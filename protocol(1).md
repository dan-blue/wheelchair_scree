# OGOA Protocol Specification: 

**Version:** 1.0  
**Status:** Draft  
**Date:** 2026-02-11

TODO: Add Hardware layer, UART? RS-485 suggested?  
---

## 1\. Introduction

### 1.1 Purpose

The purpose of this document is to define the communication protocol between the **Wheelchair System (SYSMCU)** and the **Display and Control System (DISPCTRL)**.

### 1.2 Scope

This specification covers the application-layer communication protocol, including:

* Frame structure and serialization (Wire Format).  
* Command and Telemetry definitions.  
* Flow control (Acknowledgment mechanisms).

### 1.3 Terminology

To avoid ambiguity, the following terms are used:

* **Frame:** A single unit of data transmission on the wire (max 256 bytes).  
* **Segment:** A distinct section of data within a Frame.  
* **Byte/Octet:** An 8-bit unsigned integer.

---

## 2\. Requirements

### 2.1 Data Representation

* **Endianness:** Multi-byte data types (e.g., `uint16`, `float`) **SHALL** be serialized in **Little Endian** format (Least Significant Byte first).  
* **Alignment:** Data **SHALL** be byte-aligned (pad to 1 Byte). Bit-level packing is not used; boolean flags occupy a full byte.

### 2.2 Frame Constraints

* **MTU:** A single Frame **SHALL NOT** exceed **256 bytes** in total length.  
* **Header:** Every Frame **SHALL** begin with a Status Segment (Header) that uniquely identifies the frame type or state.

### 2.3 Interaction Flow

* **Acknowledgment:** Upon successfully receiving and validating a Frame, the receiver **SHALL** transmit a Response Frame (Ack) to the sender.  
* **Timeout & Retry:** \* If an ACK is not received within **100 ms**, the sender **SHALL** re-transmit the frame.  
* If the second attempt fails, the sender will enter a **Status Request Loop**, sending a Status Request (`0x4B`) every **250 ms** until the receiver responds.

---

## 3\. Frame Structure

The protocol uses a fixed header with a variable-length payload. A **Sequence Number** has been added to the header to prevent duplicate command execution during retries.

### 3.1 Wire Format Diagram

```
 0       1       2       3       4 ... N      N+1
+-------+-------+-------+-------+------------+-------+
| Start |  Seq  | Type  |  Len  |  Payload   | Chksum|
+-------+-------+-------+-------+------------+-------+
```

## 

## 3.2 Field Definitions

| Byte Offset | Field Name | Size (Bytes) | Description |
| :---- | :---- | :---- | :---- |
| 0 | Start of Frame | 1 | Unique fixed value: `0x27` |
| 1 | Sequence Num | 1 | Rolling counter (0–255). Increments on new messages; unchanged on retries. |
| 2 | Status / Type | 1 | Type of packet being received (Sensor Data, Status, ACK, etc.). |
| 3 | Length (N) | 1 | Number of bytes in the Payload field. |
| 4 ... N | Payload | Variable | The actual data content. |
| N \+ 1 | Checksum | 1 | Bytewise XOR of the entire frame. |

---

## 4\. Packet Types & Payloads

| Type ID | Name | Description |
| :---- | :---- | :---- |
| `0x4B` | Status Request | Request receiver's status. |
| `0xB4` | Status Response | Status of device. |
| `0x67` | ACK | Acknowledge packet reception. |
| `0xAA` | LiDAR Send | Most recent measurements from LiDAR sensors. |

---

## 4.1 Payload: Status Response (`0xB4`)

Direction: SYSMCU → DISPCTRL  
Description:

| Offset | Field | Type | Unit | Description |
| :---- | :---- | :---- | :---- | :---- |
| 0 | Mode | unint8 | \- | Passthrough / Modify Mode |
| 1 | x Value | uint8 | \- | current x heading |
| 2 | y Value | uint8 | \- | current y heading |

---

## 4.2 Payload: LiDAR Send (`0xAA`)

Direction: SYSMCU → DISPCTRL  
Description: Array of distance points for obstacle detection.

| Offset | Field | Type | Unit | Description |
| :---- | :---- | :---- | :---- | :---- |
| 0 | Start Theta | unint8 | degrees | Start Theta |
| 1 | Delta Theta | uint8 | degrees | Delta Theta |
| 2 | Distances | uint16\[\] | mm | Array of distance values. |

