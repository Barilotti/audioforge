#!/usr/bin/env python3
"""Regenerates the WAV test fixtures with ffmpeg, an encoder independent of AudioForge.

Every file holds the same test signal: sample i of channel c is ((7*i + 13*c) % 200 - 100) / 128,
which every PCM depth from 8 bits up represents exactly. The decoder tests check every sample.

Usage: python3 generate.py   (needs ffmpeg on PATH; run from any directory)
"""
import pathlib
import struct
import subprocess
import tempfile

FRAMES = 301  # odd, so 8-bit mono data needs a RIFF pad byte

# Wave64 aligns chunks to 8 bytes, and ffmpeg counts that padding in the data size. The s16
# stereo file uses 302 frames (1208 bytes, already aligned); the float mono file keeps 301 frames
# and relies on its fact chunk for the true length.
FRAMES_OVERRIDE = {"w64_s16_stereo_48000.w64": 302}

FIXTURES = [
    # name, sample rate, channels, codec, container options
    ("pcm_u8_mono_44100.wav", 44100, 1, "pcm_u8", ["-f", "wav"]),
    ("pcm_s16_stereo_48000.wav", 48000, 2, "pcm_s16le", ["-f", "wav"]),
    ("pcm_s24_stereo_96000.wav", 96000, 2, "pcm_s24le", ["-f", "wav"]),
    ("pcm_s32_mono_48000.wav", 48000, 1, "pcm_s32le", ["-f", "wav"]),
    ("float32_stereo_48000.wav", 48000, 2, "pcm_f32le", ["-f", "wav"]),
    ("float64_mono_48000.wav", 48000, 1, "pcm_f64le", ["-f", "wav"]),
    ("extensible_s24_4ch_48000.wav", 48000, 4, "pcm_s24le", ["-f", "wav"]),
    ("bwf_s16_stereo_48000.wav", 48000, 2, "pcm_s16le", ["-f", "wav", "-write_bext", "1"]),
    ("rf64_s24_stereo_48000.wav", 48000, 2, "pcm_s24le", ["-f", "wav", "-rf64", "always"]),
    ("w64_s16_stereo_48000.w64", 48000, 2, "pcm_s16le", ["-f", "w64"]),
    ("w64_float32_mono_44100.w64", 44100, 1, "pcm_f32le", ["-f", "w64"]),
]


def sample(i: int, c: int) -> float:
    return ((7 * i + 13 * c) % 200 - 100) / 128.0


def main() -> None:
    out_dir = pathlib.Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory() as tmp:
        for name, rate, channels, codec, container in FIXTURES:
            raw = pathlib.Path(tmp) / f"{name}.raw"
            frames = FRAMES_OVERRIDE.get(name, FRAMES)
            raw.write_bytes(b"".join(struct.pack("<d", sample(i, c))
                                     for i in range(frames) for c in range(channels)))
            subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                            "-f", "f64le", "-ar", str(rate), "-ac", str(channels), "-i", str(raw),
                            "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact",
                            "-c:a", codec, *container, str(out_dir / name)], check=True)
            print(name)


if __name__ == "__main__":
    main()
