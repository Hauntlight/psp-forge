"""PSP-Forge Audio Cooker.

Transcodes WAV audio files into PSP hardware-compliant raw PCM binary files (.snd).
Hardware Constraints:
- 16-bit Signed Little-Endian PCM.
- 44100 Hz or 48000 Hz.
- Buffer size aligned to a multiple of 64 samples.
- Channels: 1 (Mono) or 2 (Stereo).
"""

import math
import os
import shutil
import struct
import subprocess
import wave


def resample_pcm16(
    samples: list[int],
    src_rate: int,
    dst_rate: int,
    num_channels: int
) -> list[int]:
    """Linear interpolation resampler for PCM 16-bit interleaved audio."""
    if src_rate == dst_rate:
        return samples

    src_frames = len(samples) // num_channels
    dst_frames = int(round(src_frames * (dst_rate / src_rate)))
    ratio = (src_frames - 1) / max(1, (dst_frames - 1))

    resampled = [0] * (dst_frames * num_channels)

    for i in range(dst_frames):
        src_pos = i * ratio
        idx0 = int(src_pos)
        idx1 = min(idx0 + 1, src_frames - 1)
        frac = src_pos - idx0

        for ch in range(num_channels):
            s0 = samples[idx0 * num_channels + ch]
            s1 = samples[idx1 * num_channels + ch]
            val = int(round(s0 + frac * (s1 - s0)))
            val = max(-32768, min(32767, val))
            resampled[i * num_channels + ch] = val

    return resampled


def cook_audio(
    input_path: str,
    output_path: str,
    target_rate: int = 44100,
    force_stereo: bool = True
) -> dict:
    """Cooks an audio file into a PSP .snd file."""
    ext = os.path.splitext(input_path)[1].lower()
    temp_wav_path = None

    if ext in [".mp3", ".ogg", ".flac", ".m4a"]:
        ffmpeg_bin = shutil.which("ffmpeg")
        if not ffmpeg_bin:
            raise RuntimeError(
                f"Cannot convert '{input_path}': FFmpeg is not installed. "
                "Please provide a .wav file directly or install ffmpeg."
            )
        temp_wav_path = output_path + ".tmp.wav"
        cmd = [
            ffmpeg_bin, "-y", "-i", input_path,
            "-ar", str(target_rate),
            "-ac", "2" if force_stereo else "1",
            "-acodec", "pcm_s16le",
            temp_wav_path
        ]
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if res.returncode != 0:
            raise RuntimeError(f"FFmpeg conversion failed: {res.stderr.decode('utf-8', errors='ignore')}")
        wav_file_to_read = temp_wav_path
    elif ext == ".wav":
        wav_file_to_read = input_path
    else:
        raise ValueError(f"Unsupported audio extension '{ext}'. Use .wav, .mp3, .ogg, .flac, or .m4a.")

    try:
        with wave.open(wav_file_to_read, "rb") as wf:
            nchannels = wf.getnchannels()
            sampwidth = wf.getsampwidth()
            framerate = wf.getframerate()
            nframes = wf.getnframes()
            raw_data = wf.readframes(nframes)

        # Convert to signed 16-bit integer array
        if sampwidth == 1:
            # 8-bit unsigned -> 16-bit signed
            samples = [((b - 128) << 8) for b in raw_data]
        elif sampwidth == 2:
            # 16-bit signed
            samples = list(struct.unpack(f"<{nframes * nchannels}h", raw_data))
        elif sampwidth == 3:
            # 24-bit -> 16-bit
            samples = []
            for i in range(0, len(raw_data), 3):
                b24 = raw_data[i:i+3]
                val = int.from_bytes(b24, byteorder="little", signed=True) >> 8
                samples.append(val)
        elif sampwidth == 4:
            # 32-bit -> 16-bit
            samples_32 = struct.unpack(f"<{nframes * nchannels}i", raw_data)
            samples = [s >> 16 for s in samples_32]
        else:
            raise ValueError(f"Unsupported sample width: {sampwidth * 8}-bit")

        # Convert mono to stereo if requested
        if nchannels == 1 and force_stereo:
            stereo_samples = []
            for s in samples:
                stereo_samples.append(s)
                stereo_samples.append(s)
            samples = stereo_samples
            nchannels = 2
        elif nchannels > 2:
            # Downmix to stereo
            stereo_samples = []
            for i in range(0, len(samples), nchannels):
                left = samples[i]
                right = samples[i + 1]
                stereo_samples.append(left)
                stereo_samples.append(right)
            samples = stereo_samples
            nchannels = 2

        # Resample if needed
        if framerate != target_rate:
            samples = resample_pcm16(samples, framerate, target_rate, nchannels)
            framerate = target_rate

        total_frames = len(samples) // nchannels

        # Align to multiple of 64 samples for hardware DMA buffer
        remainder = total_frames % 64
        if remainder != 0:
            padding_frames = 64 - remainder
            samples.extend([0] * (padding_frames * nchannels))
            total_frames += padding_frames

        pcm_bytes = struct.pack(f"<{len(samples)}h", *samples)

        # 32-byte Header:
        # Magic (4s), Version (H), Channels (B), BitsPerSample (B),
        # SampleRate (I), FrameCount (I), DataSize (I), Reserved (12s)
        header = struct.pack(
            "<4sHBBIII12s",
            b"PSND",
            1,
            nchannels,
            16,
            framerate,
            total_frames,
            len(pcm_bytes),
            b"\x00" * 12
        )

        os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
        with open(output_path, "wb") as f:
            f.write(header)
            f.write(pcm_bytes)

    finally:
        if temp_wav_path and os.path.exists(temp_wav_path):
            os.remove(temp_wav_path)

    file_size = len(header) + len(pcm_bytes)

    # PSP RAM Budget Warning (PSP-1000 has ~24MB usable RAM)
    if file_size > 2 * 1024 * 1024:
        print(f"  [!] WARNING (PSP Audio RAM): Sound '{os.path.basename(input_path)}' occupies {file_size / (1024 * 1024):.1f} MB in RAM.")
        print(f"      PSP-1000 has ~24MB usable RAM. Sound effects should typically be short (< 500 KB).")

    return {
        "output_path": output_path,
        "channels": nchannels,
        "sample_rate": framerate,
        "sample_count": total_frames,
        "duration_sec": total_frames / framerate,
        "file_size": file_size
    }
