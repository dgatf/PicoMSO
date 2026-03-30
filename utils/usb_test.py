#!/usr/bin/env python3

import struct
import sys
import usb.core
import usb.util

VID = 0x04B5
PID = 0x2041

# USB transport
EP_BULK_IN = 0x86

# Vendor OUT control transfer.
# In your current glue layer, only OUT transfers at STAGE_DATA are captured.
BM_REQUEST_OUT = 0x40
BREQUEST_OUT = 0x01
WVALUE = 0
WINDEX = 0

# PicoMSO protocol message types
PICOMSO_MSG_GET_INFO = 0x01
PICOMSO_MSG_GET_CAPABILITIES = 0x02
PICOMSO_MSG_GET_STATUS = 0x03
PICOMSO_MSG_SET_MODE = 0x04
PICOMSO_MSG_REQUEST_CAPTURE = 0x05
PICOMSO_MSG_READ_DATA_BLOCK = 0x06

# Response types used by firmware
PICOMSO_MSG_ACK = 0x80         # Adjust if protocol.h says otherwise
PICOMSO_MSG_ERROR = 0x81       # Adjust if protocol.h says otherwise
PICOMSO_MSG_DATA_BLOCK = 0x82

# IMPORTANT:
# These two values should match protocol.h exactly.
# Header is known to be 8 bytes and little-endian.
PICOMSO_PACKET_MAGIC = 0x4D53  # <-- replace with the real value from protocol.h if different
PICOMSO_VER_MAJOR = 0x00       # <-- replace if different
PICOMSO_VER_MINOR = 0x03       # <-- replace if different

# Assumed header layout (8 bytes total):
#   uint16_t magic
#   uint8_t  version_major
#   uint8_t  version_minor
#   uint8_t  msg_type
#   uint8_t  seq
#   uint16_t length
#
# If your protocol.h defines seq as uint16_t instead, change this format.
HEADER_FMT = "<HBBBBH"
HEADER_SIZE = struct.calcsize(HEADER_FMT)


def find_device():
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        raise RuntimeError("Device not found")

    try:
        if dev.is_kernel_driver_active(0):
            dev.detach_kernel_driver(0)
    except (NotImplementedError, usb.core.USBError):
        pass

    dev.set_configuration()
    usb.util.claim_interface(dev, 0)
    return dev


def build_packet(msg_type: int, seq: int = 1, payload: bytes = b"") -> bytes:
    header = struct.pack(
        HEADER_FMT,
        PICOMSO_PACKET_MAGIC,
        PICOMSO_VER_MAJOR,
        PICOMSO_VER_MINOR,
        msg_type,
        seq & 0xFF,
        len(payload),
    )
    return header + payload


def send_control_packet(dev, packet: bytes, timeout_ms: int = 2000):
    written = dev.ctrl_transfer(
        BM_REQUEST_OUT,
        BREQUEST_OUT,
        WVALUE,
        WINDEX,
        packet,
        timeout=timeout_ms,
    )
    if written != len(packet):
        raise RuntimeError(f"Short control write: wrote {written}, expected {len(packet)}")


def read_bulk_packet(dev, size: int = 256, timeout_ms: int = 2000) -> bytes:
    data = dev.read(EP_BULK_IN, size, timeout=timeout_ms)
    return bytes(data)


def parse_header(packet: bytes):
    if len(packet) < HEADER_SIZE:
        raise RuntimeError(f"Packet too short for header: {len(packet)} bytes")
    return struct.unpack(HEADER_FMT, packet[:HEADER_SIZE])


def dump_packet(name: str, packet: bytes):
    print(f"\n{name}: {len(packet)} bytes")
    print("RAW:", packet.hex(" "))

    magic, ver_major, ver_minor, msg_type, seq, length = parse_header(packet)
    payload = packet[HEADER_SIZE:]

    print(
        f"HEADER: magic=0x{magic:04X} "
        f"ver={ver_major}.{ver_minor} "
        f"msg_type=0x{msg_type:02X} "
        f"seq={seq} "
        f"length={length}"
    )
    print("PAYLOAD:", payload.hex(" "))

    return {
        "magic": magic,
        "ver_major": ver_major,
        "ver_minor": ver_minor,
        "msg_type": msg_type,
        "seq": seq,
        "length": length,
        "payload": payload,
    }


def parse_logic_samples(data: bytes):
    return [struct.unpack_from("<H", data, offset)[0] for offset in range(0, len(data) - (len(data) % 2), 2)]


def get_info(dev):
    packet = build_packet(PICOMSO_MSG_GET_INFO, seq=1, payload=b"")
    send_control_packet(dev, packet)
    resp = read_bulk_packet(dev, size=128)
    info = dump_packet("GET_INFO response", resp)

    payload = info["payload"]
    if len(payload) >= 34:
        proto_major = payload[0]
        proto_minor = payload[1]
        fw_id = payload[2:34].split(b"\x00", 1)[0].decode("ascii", errors="replace")
        print(f"Decoded GET_INFO: protocol={proto_major}.{proto_minor} fw_id='{fw_id}'")


def get_capabilities(dev):
    packet = build_packet(PICOMSO_MSG_GET_CAPABILITIES, seq=2, payload=b"")
    send_control_packet(dev, packet)
    resp = read_bulk_packet(dev, size=128)
    info = dump_packet("GET_CAPABILITIES response", resp)

    payload = info["payload"]
    if len(payload) >= 4:
        (caps,) = struct.unpack_from("<I", payload, 0)
        print(f"Decoded capabilities: 0x{caps:08X}")


def get_status(dev):
    packet = build_packet(PICOMSO_MSG_GET_STATUS, seq=3, payload=b"")
    send_control_packet(dev, packet)
    resp = read_bulk_packet(dev, size=128)
    info = dump_packet("GET_STATUS response", resp)

    payload = info["payload"]
    if len(payload) >= 2:
        mode = payload[0]
        capture_state = payload[1]
        print(f"Decoded status: mode={mode} capture_state={capture_state}")


def set_mode(dev, mode: int):
    payload = struct.pack("<B", mode)
    packet = build_packet(PICOMSO_MSG_SET_MODE, seq=4, payload=payload)
    send_control_packet(dev, packet)
    resp = read_bulk_packet(dev, size=128)
    dump_packet("SET_MODE response", resp)


def request_capture(dev, total_samples: int, pre_trigger_samples: int):
    payload = struct.pack("<II", total_samples, pre_trigger_samples)
    packet = build_packet(PICOMSO_MSG_REQUEST_CAPTURE, seq=5, payload=payload)
    send_control_packet(dev, packet)
    resp = read_bulk_packet(dev, size=128)
    dump_packet("REQUEST_CAPTURE response", resp)


def read_data_block(dev, seq: int):
    packet = build_packet(PICOMSO_MSG_READ_DATA_BLOCK, seq=seq, payload=b"")
    send_control_packet(dev, packet)
    resp = read_bulk_packet(dev, size=256)
    info = dump_packet("READ_DATA_BLOCK response", resp)

    payload = info["payload"]
    if len(payload) >= 4:
        block_id, data_len = struct.unpack_from("<HH", payload, 0)
        data = payload[4:4 + data_len]

        print(f"Decoded DATA_BLOCK: block_id={block_id} data_len={data_len}")
        print("Data bytes:", list(data))
        print("Interpreted logic samples (little-endian uint16):")
        print(parse_logic_samples(data))
        return data

    return b""


def main():
    dev = find_device()
    try:
        get_info(dev)
        get_capabilities(dev)
        get_status(dev)
        set_mode(dev, 1)  # 1 = PICOMSO_MODE_LOGIC
        get_status(dev)
        total_samples = 1000
        request_capture(dev, total_samples=total_samples, pre_trigger_samples=128)
        get_status(dev)

        total_bytes = total_samples * 2
        received = bytearray()
        next_seq = 6
        while len(received) < total_bytes:
            received.extend(read_data_block(dev, seq=next_seq))
            next_seq += 1

        print(f"Total capture bytes received: {len(received)}")
    finally:
        usb.util.release_interface(dev, 0)
        usb.util.dispose_resources(dev)


if __name__ == "__main__":
    try:
        main()
    except usb.core.USBError as e:
        print(f"USB error: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
