"""Tests for PSP-Forge Audio Cooker."""
import math
import os
import struct
import tempfile
import unittest
import wave

import sys
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from cli.cookers.audio import cook_audio


class TestAudioCooker(unittest.TestCase):
    def test_cook_wav(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            src_wav = os.path.join(tmpdir, "beep.wav")
            out_snd = os.path.join(tmpdir, "beep.snd")

            # Generate 100 samples sine wave at 44100Hz mono
            framerate = 44100
            num_samples = 100
            with wave.open(src_wav, "wb") as wf:
                wf.setnchannels(1)
                wf.setsampwidth(2)
                wf.setframerate(framerate)
                sine_bytes = bytearray()
                for i in range(num_samples):
                    val = int(32767.0 * math.sin(2.0 * math.pi * 440.0 * i / framerate))
                    sine_bytes.extend(struct.pack("<h", val))
                wf.writeframes(sine_bytes)

            info = cook_audio(src_wav, out_snd, target_rate=44100, force_stereo=True)
            self.assertTrue(os.path.exists(out_snd))
            self.assertEqual(info["channels"], 2)
            # Sample count aligned to 64: 100 aligned up is 128
            self.assertEqual(info["sample_count"], 128)

            with open(out_snd, "rb") as f:
                header = f.read(32)
                magic, ver, ch, bps, rate, frames, size = struct.unpack("<4sHBBIII", header[:20])
                self.assertEqual(magic, b"PSND")
                self.assertEqual(ver, 1)
                self.assertEqual(ch, 2)
                self.assertEqual(bps, 16)
                self.assertEqual(rate, 44100)
                self.assertEqual(frames, 128)
                self.assertEqual(size, 128 * 2 * 2) # 128 frames * 2 channels * 2 bytes


if __name__ == "__main__":
    unittest.main()
