#!/usr/bin/env python3
import argparse
import sys
import time
import random
import math

try:
    import serial
except ImportError:
    print("Missing dependency: pyserial. Install with: pip install pyserial")
    sys.exit(1)


START = 0x27
TYPE_STATUS_REQUEST = 0x4B
TYPE_STATUS_RESPONSE = 0xB4
TYPE_ACK = 0x67
TYPE_LIDAR_SEND = 0xAA

# Hallway map (mm): L-shaped corridor with a right turn.
WALLS = [
    ((-800.0, 0.0), (800.0, 0.0)),
    ((800.0, 0.0), (800.0, -8200.0)),
    ((800.0, -8200.0), (9000.0, -8200.0)),
    ((9000.0, -8200.0), (9000.0, -9800.0)),
    ((9000.0, -9800.0), (-800.0, -9800.0)),
    ((-800.0, -9800.0), (-800.0, 0.0)),
]


def crc_xor(data: bytes) -> int:
    c = 0
    for b in data:
        c ^= b
    return c & 0xFF


def build_frame(seq: int, ftype: int, payload: bytes = b"") -> bytes:
    payload = payload or b""
    if len(payload) > 251:
        raise ValueError("Payload too large for OGOA frame")
    head = bytes([START, seq & 0xFF, ftype & 0xFF, len(payload) & 0xFF]) + payload
    return head + bytes([crc_xor(head)])


def fmt_hex(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


def ftype_name(ftype: int) -> str:
    if ftype == TYPE_STATUS_REQUEST:
        return "STATUS_REQUEST"
    if ftype == TYPE_STATUS_RESPONSE:
        return "STATUS_RESPONSE"
    if ftype == TYPE_ACK:
        return "ACK"
    if ftype == TYPE_LIDAR_SEND:
        return "LIDAR_SEND"
    return f"UNKNOWN_0x{ftype:02X}"


def parse_frames(rx_buf: bytearray):
    out = []
    while True:
        if len(rx_buf) < 5:
            break
        try:
            start_idx = rx_buf.index(START)
        except ValueError:
            rx_buf.clear()
            break
        if start_idx > 0:
            del rx_buf[:start_idx]
        if len(rx_buf) < 5:
            break

        payload_len = rx_buf[3]
        total_len = 5 + payload_len
        if len(rx_buf) < total_len:
            break

        frame = bytes(rx_buf[:total_len])
        expected = crc_xor(frame[:-1])
        got = frame[-1]
        if expected != got:
            print(f"[RX!] Bad CRC exp={expected:02X} got={got:02X} raw={fmt_hex(frame)}")
            del rx_buf[0]
            continue

        out.append(frame)
        del rx_buf[:total_len]
    return out


def send_and_log(ser: serial.Serial, frame: bytes, tag: str):
    ser.write(frame)
    ser.flush()
    print(f"[TX ] {tag:16s} {fmt_hex(frame)}")


def ray_segment_distance_mm(
    ox: float,
    oy: float,
    dx: float,
    dy: float,
    x1: float,
    y1: float,
    x2: float,
    y2: float
):
    sx = x2 - x1
    sy = y2 - y1
    denom = (dx * sy) - (dy * sx)
    if abs(denom) < 1e-9:
        return None

    qpx = x1 - ox
    qpy = y1 - oy
    t = ((qpx * sy) - (qpy * sx)) / denom
    u = ((qpx * dy) - (qpy * dx)) / denom

    if t > 0.0 and 0.0 <= u <= 1.0:
        return t
    return None


def robot_pose_for_time(t_sec: float, loop_sec: float):
    # Piecewise path:
    # 1) straight down hallway,
    # 2) right turn around corner,
    # 3) continue down side hallway.
    if loop_sec <= 1e-6:
        loop_sec = 20.0
    p = (t_sec % loop_sec) / loop_sec

    if p < 0.55:
        u = p / 0.55
        x = 0.0
        y = -1200.0 - (7400.0 * u)
        hdg = 0.0
    elif p < 0.75:
        u = (p - 0.55) / 0.20
        x = 1800.0 * u
        y = -8600.0 - (400.0 * u)
        hdg = 90.0 * u
    else:
        u = (p - 0.75) / 0.25
        x = 1800.0 + (5200.0 * u)
        y = -9000.0
        hdg = 90.0

    return x, y, hdg


def points_for_chunk(chunk_degrees: int, delta_theta: int, phase_offset: int = 0, max_points: int = 120) -> int:
    # Number of samples to span a chunk using step=delta_theta, constrained by payload.
    usable = max(1, chunk_degrees - phase_offset)
    pts = (usable + delta_theta - 1) // delta_theta
    return max(1, min(max_points, pts))


def lidar_distance_model_mm(
    theta_deg: int,
    robot_x_mm: float,
    robot_y_mm: float,
    heading_deg: float,
    prev_mm: int,
    smooth_alpha: float,
    dropout_prob: float
) -> int:
    draw_max_mm = 3800
    no_return_mm = 4095
    ray_world_deg = (theta_deg + heading_deg) % 360.0
    rad = math.radians(ray_world_deg - 90.0)
    dx = math.cos(rad)
    dy = math.sin(rad)

    nearest = None
    for (x1, y1), (x2, y2) in WALLS:
        hit = ray_segment_distance_mm(robot_x_mm, robot_y_mm, dx, dy, x1, y1, x2, y2)
        if hit is not None and (nearest is None or hit < nearest):
            nearest = hit

    raw = no_return_mm if nearest is None else int(nearest)

    # Add realistic measurement noise and occasional glitches.
    if raw < draw_max_mm:
        raw = int(raw + random.gauss(0.0, 9.0))
        if random.random() < 0.01:
            raw = int(raw + random.gauss(0.0, 40.0))
    if random.random() < dropout_prob:
        raw = no_return_mm

    raw = max(120, min(no_return_mm, raw))

    # Temporal smoothing per angle.
    if prev_mm <= 0:
        prev_mm = draw_max_mm
    if raw >= draw_max_mm:
        # Decay toward "no return" smoothly.
        blended = int((1.0 - smooth_alpha) * prev_mm + smooth_alpha * no_return_mm)
    else:
        blended = int((1.0 - smooth_alpha) * prev_mm + smooth_alpha * raw)

    return max(120, min(no_return_mm, blended))


def main():
    ap = argparse.ArgumentParser(description="OGOA USB quick handshake tester")
    ap.add_argument("--port", required=True, help="Serial port (example: COM7)")
    ap.add_argument("--baud", type=int, default=115200, help="Baud rate")
    ap.add_argument("--duration", type=float, default=8.0, help="Run duration in seconds")
    ap.add_argument("--status-interval", type=float, default=1.0, help="Seconds between STATUS_REQUEST")
    ap.add_argument("--lidar-interval", type=float, default=0.20, help="Seconds between full fake LiDAR sweeps (0 disables)")
    ap.add_argument("--scenario-seconds", type=float, default=20.0, help="Seconds for one hallway->corner loop")
    ap.add_argument("--delta-min", type=int, default=2, help="Minimum delta theta (>=2 recommended)")
    ap.add_argument("--delta-max", type=int, default=2, help="Maximum delta theta")
    ap.add_argument("--smooth-alpha", type=float, default=0.35, help="Per-angle temporal smoothing factor [0..1]")
    ap.add_argument("--dropout-prob", type=float, default=0.01, help="Probability of a no-return sample")
    args = ap.parse_args()

    seq = 0
    next_status_t = 0.0
    next_lidar_t = 0.0
    rx_buf = bytearray()
    delta_theta = max(2, min(args.delta_min, args.delta_max))
    delta_dir = 1
    smooth_alpha = max(0.0, min(1.0, args.smooth_alpha))
    dropout_prob = max(0.0, min(0.3, args.dropout_prob))
    smoothed_ranges = [4095] * 360
    sim_t0 = time.monotonic()

    print(f"Opening {args.port} @ {args.baud}")
    with serial.Serial(args.port, args.baud, timeout=0.05, write_timeout=0.5) as ser:
        # USB CDC often needs DTR asserted + short settle delay.
        ser.dtr = True
        ser.rts = True
        time.sleep(1.2)
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        end_t = time.monotonic() + args.duration
        while time.monotonic() < end_t:
            now = time.monotonic()

            if now >= next_status_t:
                fr = build_frame(seq, TYPE_STATUS_REQUEST, b"")
                send_and_log(ser, fr, "STATUS_REQUEST")
                seq = (seq + 1) & 0xFF
                next_status_t = now + args.status_interval

            if args.lidar_interval > 0 and now >= next_lidar_t:
                sim_t = now - sim_t0
                robot_x, robot_y, robot_hdg = robot_pose_for_time(sim_t, args.scenario_seconds)
                # For delta=2 send both phases (even + odd) for full 360 coverage each sweep.
                phase_offsets = [0]
                if delta_theta == 2:
                    phase_offsets = [0, 1]

                for phase_offset in phase_offsets:
                    for base in (0, 180):
                        start_theta = base + phase_offset
                        pcount = points_for_chunk(180, delta_theta, phase_offset=phase_offset)
                        payload = bytearray([start_theta & 0xFF, delta_theta & 0xFF])
                        theta = start_theta
                        for _ in range(pcount):
                            idx = theta % 360
                            dmm = lidar_distance_model_mm(
                                idx,
                                robot_x,
                                robot_y,
                                robot_hdg,
                                smoothed_ranges[idx],
                                smooth_alpha,
                                dropout_prob
                            )
                            smoothed_ranges[idx] = dmm
                            payload.append(dmm & 0xFF)
                            payload.append((dmm >> 8) & 0xFF)
                            theta = (theta + delta_theta) % 360

                        frame = build_frame(seq, TYPE_LIDAR_SEND, bytes(payload))
                        send_and_log(
                            ser,
                            frame,
                            f"LIDAR_{base:03d} d={delta_theta} p={phase_offset} x={int(robot_x)} y={int(robot_y)} h={int(robot_hdg)}"
                        )
                        seq = (seq + 1) & 0xFF

                if args.delta_max > args.delta_min:
                    delta_theta += delta_dir
                    if delta_theta >= args.delta_max:
                        delta_theta = args.delta_max
                        delta_dir = -1
                    elif delta_theta <= args.delta_min:
                        delta_theta = args.delta_min
                        delta_dir = 1
                next_lidar_t = now + args.lidar_interval

            chunk = ser.read(256)
            if chunk:
                rx_buf.extend(chunk)
                for frame in parse_frames(rx_buf):
                    fseq = frame[1]
                    ftype = frame[2]
                    flen = frame[3]
                    payload = frame[4:4 + flen]
                    print(f"[RX ] {ftype_name(ftype):16s} seq={fseq:3d} len={flen:3d} raw={fmt_hex(frame)}")

                    # Protocol quick-test behavior:
                    # ACK every non-ACK frame so the Pico sees full handshake.
                    if ftype != TYPE_ACK:
                        ack = build_frame(fseq, TYPE_ACK, b"")
                        send_and_log(ser, ack, "ACK")

                    # If device asks for status, respond with dummy status payload [mode,x,y].
                    if ftype == TYPE_STATUS_REQUEST:
                        status_payload = bytes([1, 42, 84])
                        resp = build_frame(seq, TYPE_STATUS_RESPONSE, status_payload)
                        send_and_log(ser, resp, "STATUS_RESPONSE")
                        seq = (seq + 1) & 0xFF

            time.sleep(0.005)

    print("Done.")


if __name__ == "__main__":
    main()
