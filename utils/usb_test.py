#!/usr/bin/env python3

import argparse
import struct
import sys
import time
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
PICOMSO_MSG_ACK = 0x80
PICOMSO_MSG_ERROR = 0x81
PICOMSO_MSG_DATA_BLOCK = 0x82

# These values mirror firmware/protocol/include/protocol.h.
PICOMSO_PACKET_MAGIC = 0x4D53
PICOMSO_VER_MAJOR = 0x00
PICOMSO_VER_MINOR = 0x03
PICOMSO_DATA_BLOCK_SIZE = 64
SAMPLE_BYTES = 2
MIN_MULTI_BLOCK_COUNT = 2
DEFAULT_TOTAL_SAMPLES = 1000
DEFAULT_PRE_TRIGGER_SAMPLES = 128
DEFAULT_SAMPLE_RATE = 100000
DEFAULT_TIMEOUT_MS = 2000
DEFAULT_CAPTURE_TIMEOUT_MS = 10000
PICOMSO_TRIGGER_COUNT = 4
PICOMSO_TRIGGER_PIN_MAX = 15

PICOMSO_TRIGGER_MATCH_LEVEL_LOW = 0x00
PICOMSO_TRIGGER_MATCH_LEVEL_HIGH = 0x01
PICOMSO_TRIGGER_MATCH_EDGE_LOW = 0x02
PICOMSO_TRIGGER_MATCH_EDGE_HIGH = 0x03

PICOMSO_MODE_LOGIC = 0x01
PICOMSO_MODE_OSCILLOSCOPE = 0x02
PICOMSO_CAPTURE_IDLE = 0x00
PICOMSO_CAPTURE_RUNNING = 0x01

MODE_NAMES = {
    PICOMSO_MODE_LOGIC: "logic",
    PICOMSO_MODE_OSCILLOSCOPE: "scope",
}

MODE_VALUES = {
    "logic": PICOMSO_MODE_LOGIC,
    "scope": PICOMSO_MODE_OSCILLOSCOPE,
}

TRIGGER_MATCH_VALUES = {
    "level-low": PICOMSO_TRIGGER_MATCH_LEVEL_LOW,
    "level-high": PICOMSO_TRIGGER_MATCH_LEVEL_HIGH,
    "edge-low": PICOMSO_TRIGGER_MATCH_EDGE_LOW,
    "edge-high": PICOMSO_TRIGGER_MATCH_EDGE_HIGH,
}

TRIGGER_MATCH_NAMES = {value: key for key, value in TRIGGER_MATCH_VALUES.items()}

DEFAULT_TRIGGER_CONFIGS = (
    {"is_enabled": 1, "pin": 0, "match": PICOMSO_TRIGGER_MATCH_EDGE_HIGH},
    {"is_enabled": 0, "pin": 0, "match": PICOMSO_TRIGGER_MATCH_LEVEL_LOW},
    {"is_enabled": 0, "pin": 0, "match": PICOMSO_TRIGGER_MATCH_LEVEL_LOW},
    {"is_enabled": 0, "pin": 0, "match": PICOMSO_TRIGGER_MATCH_LEVEL_LOW},
)

# protocol.h packet header layout:
#   uint16_t magic
#   uint8_t  version_major
#   uint8_t  version_minor
#   uint8_t  msg_type
#   uint8_t  seq
#   uint16_t length
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


def send_control_packet(dev, packet: bytes, timeout_ms: int = DEFAULT_TIMEOUT_MS):
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


def read_bulk_packet(dev, size: int = 256, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> bytes:
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


def ensure_protocol_header(info, expected_seq: int):
    if info["magic"] != PICOMSO_PACKET_MAGIC:
        raise RuntimeError(f"Unexpected packet magic: 0x{info['magic']:04X}")
    if info["ver_major"] != PICOMSO_VER_MAJOR or info["ver_minor"] != PICOMSO_VER_MINOR:
        raise RuntimeError(
            f"Unexpected protocol version {info['ver_major']}.{info['ver_minor']} "
            f"(expected {PICOMSO_VER_MAJOR}.{PICOMSO_VER_MINOR})"
        )
    if info["seq"] != (expected_seq & 0xFF):
        raise RuntimeError(f"Unexpected response seq {info['seq']}, expected {expected_seq & 0xFF}")
    if info["length"] != len(info["payload"]):
        raise RuntimeError(
            f"Header length {info['length']} does not match payload bytes {len(info['payload'])}"
        )


def decode_error(info):
    payload = info["payload"]
    if len(payload) < 2:
        return "malformed error payload"
    status = payload[0]
    msg_len = payload[1]
    if 2 + msg_len > len(payload):
        raise RuntimeError("error msg_len exceeds payload size")
    message = payload[2:2 + msg_len].decode("utf-8", errors="replace")
    return f"status=0x{status:02X} message='{message}'"


def expect_msg_type(info, expected_type: int, context: str):
    if info["msg_type"] == PICOMSO_MSG_ERROR:
        raise RuntimeError(f"{context} failed: {decode_error(info)}")
    if info["msg_type"] != expected_type:
        raise RuntimeError(
            f"{context} returned msg_type=0x{info['msg_type']:02X}, expected 0x{expected_type:02X}"
        )


def send_request(
    dev,
    name: str,
    msg_type: int,
    seq: int,
    payload: bytes,
    read_size: int = 256,
    timeout_ms: int = DEFAULT_TIMEOUT_MS,
):
    packet = build_packet(msg_type, seq=seq, payload=payload)
    send_control_packet(dev, packet, timeout_ms=timeout_ms)
    resp = read_bulk_packet(dev, size=read_size, timeout_ms=timeout_ms)
    info = dump_packet(name, resp)
    ensure_protocol_header(info, expected_seq=seq)
    return info


def mode_name(mode: int) -> str:
    return MODE_NAMES.get(mode, f"unknown(0x{mode:02X})")


def parse_u16_samples(data: bytes):
    return [
        struct.unpack_from("<H", data, offset)[0]
        for offset in range(0, len(data) - (len(data) % SAMPLE_BYTES), SAMPLE_BYTES)
    ]


def default_trigger_config():
    return {"is_enabled": 0, "pin": 0, "match": PICOMSO_TRIGGER_MATCH_LEVEL_LOW}


def parse_trigger_spec(spec: str):
    parts = spec.split(":")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError(
            "trigger must use SLOT:PIN:MATCH (for example 0:3:edge-high)"
        )

    try:
        slot = int(parts[0], 0)
        pin = int(parts[1], 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("trigger slot and pin must be integers") from exc

    match_key = parts[2].strip().lower().replace("_", "-")
    if slot < 0 or slot >= PICOMSO_TRIGGER_COUNT:
        raise argparse.ArgumentTypeError(
            f"trigger slot must be between 0 and {PICOMSO_TRIGGER_COUNT - 1}"
        )
    if pin < 0 or pin > PICOMSO_TRIGGER_PIN_MAX:
        raise argparse.ArgumentTypeError(
            f"trigger pin must be between 0 and {PICOMSO_TRIGGER_PIN_MAX}"
        )
    if match_key not in TRIGGER_MATCH_VALUES:
        raise argparse.ArgumentTypeError(
            "trigger match must be one of "
            + ", ".join(sorted(TRIGGER_MATCH_VALUES))
        )

    return {
        "slot": slot,
        "is_enabled": 1,
        "pin": pin,
        "match": TRIGGER_MATCH_VALUES[match_key],
    }


def build_trigger_configs(trigger_specs):
    if trigger_specs is None:
        return [dict(trigger) for trigger in DEFAULT_TRIGGER_CONFIGS]

    configs = [default_trigger_config() for _ in range(PICOMSO_TRIGGER_COUNT)]
    seen_slots = set()
    for spec in trigger_specs:
        if spec["slot"] in seen_slots:
            raise ValueError(f"duplicate trigger slot {spec['slot']}")
        seen_slots.add(spec["slot"])
        configs[spec["slot"]] = {
            "is_enabled": spec["is_enabled"],
            "pin": spec["pin"],
            "match": spec["match"],
        }

    return configs


def build_capture_request_payload(
    total_samples: int,
    rate: int,
    pre_trigger_samples: int,
    triggers,
) -> bytes:
    if len(triggers) != PICOMSO_TRIGGER_COUNT:
        raise ValueError(f"expected {PICOMSO_TRIGGER_COUNT} trigger slots, got {len(triggers)}")

    payload = bytearray(struct.pack("<III", total_samples, rate, pre_trigger_samples))
    for trigger in triggers:
        payload.extend(
            struct.pack("<BBB", trigger["is_enabled"], trigger["pin"], trigger["match"])
        )

    return bytes(payload)


def get_info(dev, seq: int):
    info = send_request(dev, "GET_INFO response", PICOMSO_MSG_GET_INFO, seq, b"", read_size=128)
    expect_msg_type(info, PICOMSO_MSG_ACK, "GET_INFO")

    payload = info["payload"]
    if len(payload) >= 34:
        proto_major = payload[0]
        proto_minor = payload[1]
        fw_id = payload[2:34].split(b"\x00", 1)[0].decode("ascii", errors="replace")
        print(f"Decoded GET_INFO: protocol={proto_major}.{proto_minor} fw_id='{fw_id}'")


def get_capabilities(dev, seq: int):
    info = send_request(dev, "GET_CAPABILITIES response", PICOMSO_MSG_GET_CAPABILITIES, seq, b"", read_size=128)
    expect_msg_type(info, PICOMSO_MSG_ACK, "GET_CAPABILITIES")

    payload = info["payload"]
    if len(payload) >= 4:
        (caps,) = struct.unpack_from("<I", payload, 0)
        print(f"Decoded capabilities: 0x{caps:08X}")


def get_status(dev, seq: int):
    info = send_request(dev, "GET_STATUS response", PICOMSO_MSG_GET_STATUS, seq, b"", read_size=128)
    expect_msg_type(info, PICOMSO_MSG_ACK, "GET_STATUS")

    payload = info["payload"]
    if len(payload) >= 2:
        mode = payload[0]
        capture_state = payload[1]
        print(f"Decoded status: mode={mode} capture_state={capture_state}")
        return {
            "mode": mode,
            "capture_state": capture_state,
        }

    raise RuntimeError("GET_STATUS response payload too short")


def wait_for_capture_idle(
    dev,
    start_seq: int,
    expected_mode: int,
    timeout_s: float,
    poll_interval_s: float,
):
    deadline = time.monotonic() + timeout_s
    seq = start_seq

    while True:
        status = get_status(dev, seq)
        seq += 1
        if status["mode"] != expected_mode:
            raise RuntimeError(
                f"Device left {mode_name(expected_mode)} mode unexpectedly: "
                f"mode={mode_name(status['mode'])}"
            )
        if status["capture_state"] == PICOMSO_CAPTURE_IDLE:
            return seq
        if status["capture_state"] != PICOMSO_CAPTURE_RUNNING:
            raise RuntimeError(f"Unknown capture state: {status['capture_state']}")
        if time.monotonic() >= deadline:
            raise RuntimeError("Timed out waiting for capture completion")
        time.sleep(poll_interval_s)


def set_mode(dev, seq: int, mode: int):
    payload = struct.pack("<B", mode)
    info = send_request(dev, "SET_MODE response", PICOMSO_MSG_SET_MODE, seq, payload, read_size=128)
    expect_msg_type(info, PICOMSO_MSG_ACK, "SET_MODE")


def request_capture(
    dev,
    seq: int,
    total_samples: int,
    rate: int,
    pre_trigger_samples: int,
    triggers,
    timeout_ms: int,
):
    payload = build_capture_request_payload(
        total_samples=total_samples,
        rate=rate,
        pre_trigger_samples=pre_trigger_samples,
        triggers=triggers,
    )
    for index, trigger in enumerate(triggers):
        enabled = bool(trigger["is_enabled"])
        match_name = TRIGGER_MATCH_NAMES[trigger["match"]]
        print(
            f"REQUEST_CAPTURE trigger[{index}]: enabled={enabled} "
            f"pin={trigger['pin']} match={match_name}"
        )
    info = send_request(
        dev,
        "REQUEST_CAPTURE response",
        PICOMSO_MSG_REQUEST_CAPTURE,
        seq,
        payload,
        read_size=128,
        timeout_ms=timeout_ms,
    )
    expect_msg_type(info, PICOMSO_MSG_ACK, "REQUEST_CAPTURE")


def read_data_block(dev, seq: int):
    info = send_request(dev, "READ_DATA_BLOCK response", PICOMSO_MSG_READ_DATA_BLOCK, seq, b"")
    expect_msg_type(info, PICOMSO_MSG_DATA_BLOCK, "READ_DATA_BLOCK")

    payload = info["payload"]
    if len(payload) < 4:
        raise RuntimeError("READ_DATA_BLOCK payload too short")

    block_id, data_len = struct.unpack_from("<HH", payload, 0)
    if data_len > PICOMSO_DATA_BLOCK_SIZE:
        raise RuntimeError(f"READ_DATA_BLOCK data_len {data_len} exceeds {PICOMSO_DATA_BLOCK_SIZE}")
    if len(payload) < 4 + data_len:
        raise RuntimeError(
            f"READ_DATA_BLOCK payload truncated: need {4 + data_len} bytes, got {len(payload)}"
        )

    data = payload[4:4 + data_len]
    print(f"Decoded DATA_BLOCK: block_id={block_id} data_len={data_len}")
    print("Data bytes:", list(data))
    print("Interpreted samples (little-endian uint16):")
    print(parse_u16_samples(data))
    return {
        "block_id": block_id,
        "data_len": data_len,
        "data": data,
    }


def read_completed_capture(dev, start_seq: int, total_samples: int):
    expected_total_bytes = total_samples * SAMPLE_BYTES
    received = bytearray()
    seq = start_seq
    expected_block_id = 0

    while len(received) < expected_total_bytes:
        block = read_data_block(dev, seq)
        seq += 1

        if block["block_id"] != expected_block_id:
            raise RuntimeError(
                f"Unexpected block_id {block['block_id']}, expected {expected_block_id}"
            )
        if block["data_len"] == 0:
            raise RuntimeError("READ_DATA_BLOCK returned an empty chunk before capture completion")

        remaining = expected_total_bytes - len(received)
        if block["data_len"] > remaining:
            raise RuntimeError(
                f"READ_DATA_BLOCK exceeded requested capture size: "
                f"chunk={block['data_len']} remaining={remaining}"
            )

        received.extend(block["data"])
        expected_block_id += 1

    if len(received) != expected_total_bytes:
        raise RuntimeError(
            f"Capture length mismatch: received {len(received)} bytes, expected {expected_total_bytes}"
        )

    if expected_total_bytes > PICOMSO_DATA_BLOCK_SIZE and expected_block_id < MIN_MULTI_BLOCK_COUNT:
        raise RuntimeError("Expected multi-chunk readout but received fewer than two data blocks")

    print(
        f"Verified completed capture readout: {len(received)} bytes "
        f"({len(received) // SAMPLE_BYTES} samples) across {expected_block_id} chunk(s)"
    )
    return bytes(received), seq


def parse_args():
    parser = argparse.ArgumentParser(
        description="Exercise PicoMSO REQUEST_CAPTURE + READ_DATA_BLOCK semantics."
    )
    parser.add_argument(
        "--mode",
        choices=sorted(MODE_VALUES),
        default="logic",
        help="Capture mode to select before requesting capture",
    )
    parser.add_argument(
        "--total-samples",
        type=int,
        default=DEFAULT_TOTAL_SAMPLES,
        help="Total number of samples to request and verify",
    )
    parser.add_argument(
        "--pre-trigger-samples",
        type=int,
        default=DEFAULT_PRE_TRIGGER_SAMPLES,
        help="Requested pre-trigger sample count",
    )
    parser.add_argument(
        "--rate",
        type=int,
        default=DEFAULT_SAMPLE_RATE,
        help="Requested sample rate in Hz",
    )
    parser.add_argument(
        "--capture-timeout-ms",
        type=int,
        default=DEFAULT_CAPTURE_TIMEOUT_MS,
        help="USB timeout for REQUEST_CAPTURE completion ACK",
    )
    parser.add_argument(
        "--status-timeout-s",
        type=float,
        default=2.0,
        help="Maximum time to wait for GET_STATUS to report capture idle after ACK",
    )
    parser.add_argument(
        "--status-poll-interval-s",
        type=float,
        default=0.05,
        help="Delay between GET_STATUS polls while waiting for capture completion",
    )
    parser.add_argument(
        "--trigger",
        action="append",
        type=parse_trigger_spec,
        metavar="SLOT:PIN:MATCH",
        help=(
            "Set one trigger slot in the REQUEST_CAPTURE payload "
            "(for example 0:3:edge-high). May be specified up to four times. "
            "If omitted, the script uses the firmware's previous default layout."
        ),
    )
    return parser.parse_args()


def main():
    args = parse_args()
    if args.total_samples <= 0:
        raise ValueError("--total-samples must be positive")
    if args.rate <= 0:
        raise ValueError("--rate must be positive")
    if args.pre_trigger_samples < 0:
        raise ValueError("--pre-trigger-samples must be non-negative")
    if args.pre_trigger_samples > args.total_samples:
        raise ValueError("--pre-trigger-samples must not exceed --total-samples")
    if args.trigger is not None and len(args.trigger) > PICOMSO_TRIGGER_COUNT:
        raise ValueError(f"--trigger may be specified at most {PICOMSO_TRIGGER_COUNT} times")

    trigger_configs = build_trigger_configs(args.trigger)

    dev = find_device()
    try:
        selected_mode = MODE_VALUES[args.mode]
        next_seq = 1
        get_info(dev, next_seq)
        next_seq += 1
        get_capabilities(dev, next_seq)
        next_seq += 1
        get_status(dev, next_seq)
        next_seq += 1
        set_mode(dev, next_seq, selected_mode)
        next_seq += 1
        status = get_status(dev, next_seq)
        next_seq += 1
        if status["mode"] != selected_mode:
            raise RuntimeError(
                f"SET_MODE({mode_name(selected_mode).upper()}) did not take effect: "
                f"mode={mode_name(status['mode'])}"
            )

        request_capture(
            dev,
            next_seq,
            total_samples=args.total_samples,
            rate=args.rate,
            pre_trigger_samples=args.pre_trigger_samples,
            triggers=trigger_configs,
            timeout_ms=args.capture_timeout_ms,
        )
        next_seq += 1

        next_seq = wait_for_capture_idle(
            dev,
            start_seq=next_seq,
            expected_mode=selected_mode,
            timeout_s=args.status_timeout_s,
            poll_interval_s=args.status_poll_interval_s,
        )

        read_completed_capture(dev, next_seq, total_samples=args.total_samples)
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
