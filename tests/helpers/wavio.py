"""Reads the recordings scg writes.

A recording is a RIFF/WAVE file of 32 bit IEEE floats with the i and q of every complex
channel next to each other, so a file of one complex channel has two wave channels. The
suite only depends on pytest, so the parsing is done here rather than with a library.
"""

import array
import struct

_WAVE_FORMAT_IEEE_FLOAT = 3


class WavError(Exception):
    pass


def read_iq(path):
    """Returns (sampleRate, channels), channels being a list of lists of complex."""
    raw = path.read_bytes()

    if raw[0:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise WavError(f"{path} is not a riff/wave file")

    fmt = None
    data = None

    # walk the chunks rather than assuming fmt is followed by data: the writer is free to
    # put others in between, and does when it records the center frequency
    i = 12
    while i + 8 <= len(raw):
        chunkId = raw[i:i + 4]
        (chunkSize,) = struct.unpack_from("<I", raw, i + 4)
        body = raw[i + 8:i + 8 + chunkSize]
        if chunkId == b"fmt ":
            fmt = body
        elif chunkId == b"data":
            data = body
        i += 8 + chunkSize + (chunkSize & 1)  # chunks are padded to even lengths

    if fmt is None or data is None:
        raise WavError(f"{path} has no fmt or no data chunk")

    formatTag, nWaveChannels, sampleRate = struct.unpack_from("<HHI", fmt, 0)
    (bitsPerSample,) = struct.unpack_from("<H", fmt, 14)

    if formatTag != _WAVE_FORMAT_IEEE_FLOAT or bitsPerSample != 32:
        raise WavError(
            f"{path} is format {formatTag} at {bitsPerSample} bits, "
            f"expected ieee float at 32"
        )
    if nWaveChannels % 2 != 0:
        raise WavError(f"{path} has {nWaveChannels} wave channels, which is not i/q pairs")

    samples = array.array("f")
    samples.frombytes(data[:len(data) - len(data) % 4])
    if struct.pack("=f", 1.0) != struct.pack("<f", 1.0):
        samples.byteswap()

    nComplex = nWaveChannels // 2
    channels = [[] for _ in range(nComplex)]
    for frame in range(len(samples) // nWaveChannels):
        base = frame * nWaveChannels
        for c in range(nComplex):
            channels[c].append(complex(samples[base + 2 * c], samples[base + 2 * c + 1]))

    return sampleRate, channels


def rms(values):
    if not values:
        return 0.0
    return (sum(abs(v) ** 2 for v in values) / len(values)) ** 0.5


def magnitude_spread(values):
    """How much the magnitude moves about, relative to its mean.

    Near zero for a tone of constant amplitude, well above zero for noise. Keeps the
    checks free of any particular amplitude, which the mapping from power_dbm still owes
    a proper definition.
    """
    if not values:
        return 0.0
    mags = [abs(v) for v in values]
    mean = sum(mags) / len(mags)
    if mean == 0.0:
        return 0.0
    variance = sum((m - mean) ** 2 for m in mags) / len(mags)
    return variance ** 0.5 / mean
