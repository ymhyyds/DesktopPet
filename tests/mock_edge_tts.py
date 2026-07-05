#!/usr/bin/env python3
"""Mock edge-tts CLI for DesktopPet TTS unit tests.

Simulates edge-tts behavior without hitting the real Edge TTS service.
Controlled via environment variables:

    MOCK_EDGE_EXIT_CODE   : exit code (default 0)
    MOCK_EDGE_DELAY_SEC   : seconds to sleep before exit (default 0)
    MOCK_EDGE_OUTPUT_SIZE : output file size in bytes (default 0 = write valid WAV)
                            Non-zero values produce that many bytes of valid WAV.
    MOCK_EDGE_NO_OUTPUT   : if set (any value), don't create the output file

Parses --file, --voice, --rate, --volume, --pitch, --write-media args
and writes a minimal PCM WAV file to --write-media <path>.
This WAV is playable by QMediaPlayer, allowing full playback-chain testing.
"""

import sys
import os
import time
import struct


def write_wav(path, approx_size=4096):
    """Write a minimal valid PCM WAV file with an approximation of approx_size bytes.

    PCM format: 8000 Hz, mono, 16-bit signed.
    The real size may differ slightly due to header alignment; callers who
    need a specific data size should set approx_size accordingly.
    """
    SAMPLE_RATE = 8000
    BITS_PER_SAMPLE = 16
    CHANNELS = 1

    # data chunk size — round down to even so 16-bit samples align
    data_bytes = max(0, approx_size - 44) & ~1
    if data_bytes == 0:
        # Still write a minimal WAV (just the header + 1 sample = 46 bytes)
        data_bytes = 2

    with open(path, 'wb') as f:
        # ── RIFF header ────────────────────────────────────────────────────
        f.write(b'RIFF')
        # total file size minus 8-byte RIFF header
        f.write(struct.pack('<I', 36 + data_bytes))
        f.write(b'WAVE')

        # ── fmt chunk ──────────────────────────────────────────────────────
        f.write(b'fmt ')
        f.write(struct.pack('<I', 16))       # chunk size (PCM = 16)
        f.write(struct.pack('<H', 1))        # audio format (PCM = 1)
        f.write(struct.pack('<H', CHANNELS))
        f.write(struct.pack('<I', SAMPLE_RATE))
        byte_rate = SAMPLE_RATE * CHANNELS * BITS_PER_SAMPLE // 8
        f.write(struct.pack('<I', byte_rate))
        block_align = CHANNELS * BITS_PER_SAMPLE // 8
        f.write(struct.pack('<H', block_align))
        f.write(struct.pack('<H', BITS_PER_SAMPLE))

        # ── data chunk ─────────────────────────────────────────────────────
        f.write(b'data')
        f.write(struct.pack('<I', data_bytes))
        # Silence (all zeros)
        f.write(b'\x00' * data_bytes)


def main():
    out_file = None
    args = sys.argv[1:]

    i = 0
    while i < len(args):
        if args[i] == '--write-media' and i + 1 < len(args):
            out_file = args[i + 1]
            i += 2
        elif args[i] == '--file':
            i += 2
        elif args[i] == '--voice':
            i += 2
        elif args[i] == '--rate':
            i += 2
        elif args[i] == '--volume':
            i += 2
        elif args[i] == '--pitch':
            i += 2
        else:
            i += 1

    delay = float(os.environ.get('MOCK_EDGE_DELAY_SEC', '0'))
    exit_code = int(os.environ.get('MOCK_EDGE_EXIT_CODE', '0'))
    output_size_raw = os.environ.get('MOCK_EDGE_OUTPUT_SIZE')
    no_output = os.environ.get('MOCK_EDGE_NO_OUTPUT') is not None

    if delay > 0:
        time.sleep(delay)

    if out_file and not no_output:
        if output_size_raw is None:
            # Default: write a ~4 KiB valid WAV (playable by QMediaPlayer)
            write_wav(out_file, 4096)
        elif int(output_size_raw) > 0:
            # Specific size requested — write valid WAV of approximately that size
            write_wav(out_file, int(output_size_raw))
        else:
            # MOCK_EDGE_OUTPUT_SIZE=0 → empty file (used by testEmptyOutput)
            open(out_file, 'wb').close()

    sys.exit(exit_code)


if __name__ == '__main__':
    main()
