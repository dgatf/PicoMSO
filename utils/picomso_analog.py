#!/usr/bin/env python3
"""
picomso_analog.py – PicoMSO host-side multi-channel analog driver utility.

Responsibilities (mirrors the Driver requirements in the problem statement):

1. Validate user sample requests against the per-channel maximum:
       per_channel_max = SCOPE_CAPTURE_MAX_SAMPLES / num_channels

2. Convert between user-facing per-channel samples and the total interleaved
   sample count sent to firmware:
       total_adc_samples = per_channel_samples * num_channels

3. Build the REQUEST_CAPTURE payload including the analog_channels bitmask.

4. Demultiplex the interleaved byte stream returned by the firmware into
   per-channel sample lists.

Wire format (protocol minor version 4):
  REQUEST_CAPTURE payload:
    uint32  total_samples      – total interleaved ADC samples
    uint32  rate               – sample rate in Hz (per ADC tick)
    uint32  pre_trigger_samples– total interleaved pre-trigger samples
    3 bytes trigger[0..3]      – 4 x { is_enabled, pin, match }
    uint8   analog_channels    – ADC input bitmask (bit0=ADC0, bit1=ADC1, bit2=ADC2)

  DATA_BLOCK data[] is a flat array of little-endian uint16 samples in the
  round-robin order: ADC0, ADC1, ..., ADC0, ADC1, ...

Usage example (standalone):

    import picomso_analog
    result = picomso_analog.capture_analog(
        device,
        per_channel_samples=1000,
        rate=500_000,
        analog_channels=[0, 1],   # ADC inputs 0 and 1
    )
    ch0 = result[0]   # list of 1000 uint16 values from ADC input 0
    ch1 = result[1]   # list of 1000 uint16 values from ADC input 1
"""

import struct
from typing import Dict, List, Sequence

# -------------------------------------------------------------------------
# Constants (must match firmware/mixed_signal/include/scope_capture.h)
# -------------------------------------------------------------------------

SCOPE_CAPTURE_MAX_SAMPLES = 50_000        # total analog buffer (interleaved)
SCOPE_CAPTURE_PRE_TRIGGER_MAX_SAMPLES = 4_096
# Number of available ADC input channels; valid indices are 0 to SCOPE_CAPTURE_ANALOG_CHANNEL_MAX-1.
SCOPE_CAPTURE_ANALOG_CHANNEL_MAX = 3      # ADC inputs 0..2 (GPIO 26..28)

# REQUEST_CAPTURE fixed trigger array: 4 entries × 3 bytes each.
_TRIGGER_COUNT = 4
_TRIGGER_ENTRY_FMT = "BBB"  # is_enabled, pin, match


# -------------------------------------------------------------------------
# Validation helpers
# -------------------------------------------------------------------------

def _validate_analog_channels(analog_channels: Sequence[int]) -> int:
    """
    Validate the list of ADC input indices and return the channel bitmask.

    :param analog_channels: List of ADC input indices (0, 1, or 2).
    :returns: Bitmask suitable for REQUEST_CAPTURE.analog_channels.
    :raises ValueError: On invalid or empty input.
    """
    if not analog_channels:
        raise ValueError("analog_channels must not be empty")

    bitmask = 0
    for ch in analog_channels:
        if ch < 0 or ch >= SCOPE_CAPTURE_ANALOG_CHANNEL_MAX:
            raise ValueError(
                f"ADC input index {ch} is out of range "
                f"[0, {SCOPE_CAPTURE_ANALOG_CHANNEL_MAX - 1}]"
            )
        bitmask |= 1 << ch

    if bin(bitmask).count("1") != len(set(analog_channels)):
        raise ValueError("analog_channels contains duplicate entries")

    return bitmask


def validate_sample_request(
    per_channel_samples: int,
    analog_channels: Sequence[int],
    pre_trigger_per_channel: int = 0,
) -> None:
    """
    Validate a user sample request before sending it to firmware.

    :param per_channel_samples: Number of samples requested per channel.
    :param analog_channels: List of enabled ADC input indices.
    :param pre_trigger_per_channel: Pre-trigger samples per channel.
    :raises ValueError: If the request exceeds firmware limits.
    """
    num_channels = len(set(analog_channels))
    per_channel_max = SCOPE_CAPTURE_MAX_SAMPLES // num_channels
    pre_trigger_max = SCOPE_CAPTURE_PRE_TRIGGER_MAX_SAMPLES // num_channels

    if per_channel_samples <= 0:
        raise ValueError("per_channel_samples must be > 0")

    if per_channel_samples > per_channel_max:
        raise ValueError(
            f"Requested {per_channel_samples} samples/channel exceeds "
            f"maximum {per_channel_max} for {num_channels} channel(s) "
            f"(total buffer = {SCOPE_CAPTURE_MAX_SAMPLES})"
        )

    if pre_trigger_per_channel > per_channel_samples:
        raise ValueError("pre_trigger_per_channel exceeds per_channel_samples")

    if pre_trigger_per_channel > pre_trigger_max:
        raise ValueError(
            f"Requested {pre_trigger_per_channel} pre-trigger samples/channel "
            f"exceeds maximum {pre_trigger_max} for {num_channels} channel(s) "
            f"(pre-trigger buffer = {SCOPE_CAPTURE_PRE_TRIGGER_MAX_SAMPLES})"
        )


# -------------------------------------------------------------------------
# Protocol packet building
# -------------------------------------------------------------------------

def build_request_capture_payload(
    per_channel_samples: int,
    rate: int,
    analog_channels: Sequence[int],
    pre_trigger_per_channel: int = 0,
    triggers: Sequence[dict] = (),
) -> bytes:
    """
    Build the REQUEST_CAPTURE payload for a multi-channel analog capture.

    The firmware expects total interleaved sample counts; this function
    performs the per-channel → total conversion.

    :param per_channel_samples: User-facing sample count per channel.
    :param rate: ADC clock rate in Hz (applied per ADC tick, i.e. per round-robin cycle).
    :param analog_channels: List of ADC input indices to enable (e.g. [0, 1]).
    :param pre_trigger_per_channel: Pre-trigger samples per channel.
    :param triggers: Up to 4 trigger dicts with keys is_enabled, pin, match.
    :returns: Packed payload bytes ready to append after the protocol header.
    """
    validate_sample_request(per_channel_samples, analog_channels, pre_trigger_per_channel)

    channel_mask = _validate_analog_channels(analog_channels)
    num_channels = bin(channel_mask).count("1")

    # Convert user (per-channel) counts to total interleaved counts.
    total_samples = per_channel_samples * num_channels
    pre_trigger_samples = pre_trigger_per_channel * num_channels

    # Build the 4-entry trigger array (zero-padded).
    trigger_bytes = bytearray(_TRIGGER_COUNT * 3)
    for i, trig in enumerate(triggers[:_TRIGGER_COUNT]):
        offset = i * 3
        trigger_bytes[offset] = int(trig.get("is_enabled", 0))
        trigger_bytes[offset + 1] = int(trig.get("pin", 0))
        trigger_bytes[offset + 2] = int(trig.get("match", 0))

    payload = struct.pack("<III", total_samples, rate, pre_trigger_samples)
    payload += bytes(trigger_bytes)
    payload += struct.pack("<B", channel_mask)

    return payload


# -------------------------------------------------------------------------
# Demultiplexing
# -------------------------------------------------------------------------

def demux_analog_samples(
    raw_bytes: bytes,
    analog_channels: Sequence[int],
) -> Dict[int, List[int]]:
    """
    Demultiplex an interleaved analog sample stream from the firmware.

    The firmware delivers samples in fixed round-robin order:
        ADC0, ADC1, ADC2, ADC0, ADC1, ADC2, ...
    where only the requested inputs appear, cycling through them in
    ascending ADC input order.

    :param raw_bytes: Raw bytes from READ_DATA_BLOCK data[] fields
                      (concatenated, little-endian uint16 samples).
    :param analog_channels: The same ADC input indices used for capture.
    :returns: Dict mapping ADC input index → list of uint16 sample values.
    :raises ValueError: If raw_bytes length is not a multiple of 2 or
                        not evenly divisible across the number of channels.
    """
    if len(raw_bytes) % 2 != 0:
        raise ValueError("raw_bytes length must be a multiple of 2 (uint16 samples)")

    # Determine the canonical round-robin channel ordering (ascending).
    ordered_channels = sorted(set(analog_channels))
    num_channels = len(ordered_channels)

    total_samples = len(raw_bytes) // 2
    if total_samples % num_channels != 0:
        raise ValueError(
            f"Total sample count {total_samples} is not divisible by "
            f"channel count {num_channels}"
        )

    result: Dict[int, List[int]] = {ch: [] for ch in ordered_channels}

    for i in range(total_samples):
        sample = struct.unpack_from("<H", raw_bytes, i * 2)[0]
        ch = ordered_channels[i % num_channels]
        result[ch].append(sample)

    return result


# -------------------------------------------------------------------------
# Convenience wrapper (requires a transport object with send/recv methods)
# -------------------------------------------------------------------------

def capture_analog(
    device,
    per_channel_samples: int,
    rate: int,
    analog_channels: Sequence[int],
    pre_trigger_per_channel: int = 0,
    triggers: Sequence[dict] = (),
) -> Dict[int, List[int]]:
    """
    Perform a full analog capture and return demultiplexed per-channel data.

    :param device: Object with send_request_capture(payload) and
                   read_all_scope_data() → bytes methods.
    :param per_channel_samples: Samples requested per channel.
    :param rate: ADC clock rate in Hz.
    :param analog_channels: List of ADC input indices to capture.
    :param pre_trigger_per_channel: Pre-trigger samples per channel.
    :param triggers: Optional trigger configuration list.
    :returns: Dict mapping ADC input index → list of uint16 samples.
    """
    payload = build_request_capture_payload(
        per_channel_samples=per_channel_samples,
        rate=rate,
        analog_channels=analog_channels,
        pre_trigger_per_channel=pre_trigger_per_channel,
        triggers=triggers,
    )

    device.send_request_capture(payload)
    raw_bytes = device.read_all_scope_data()

    return demux_analog_samples(raw_bytes, analog_channels)
