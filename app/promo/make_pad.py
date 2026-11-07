#!/usr/bin/env python3
"""A quiet pad for the promo when the ASyn bed isn't there: A minor, about 75 bpm
(Am, F, C, G, a bar each), soft detuned partials, about -24 LUFS.

    make_pad.py out.wav
"""

import os
import subprocess
import sys
import wave

import numpy as np

RATE, DURATION, BPM = 48000, 62.0, 75.0


def note(name):
    names = {"C": -9, "D": -7, "E": -5, "F": -4, "G": -2, "A": 0, "B": 2}
    return 440.0 * 2 ** ((names[name[0]] + 12 * (int(name[1]) - 4)) / 12)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "bed.wav"
    chords = [["A2", "E3", "A3", "C4", "E4"], ["F2", "C3", "A3", "C4", "F4"], ["C3", "G3", "C4", "E4", "G4"], ["G2", "D3", "G3", "B3", "D4"]]
    bar = 4 * 60.0 / BPM
    n = int(RATE * DURATION)
    t = np.arange(n) / RATE
    left, right = np.zeros(n), np.zeros(n)
    rng = np.random.default_rng(7)
    k = 0
    start = 0.0
    while start < DURATION:
        chord = chords[k % len(chords)]
        a, b = int(start * RATE), min(n, int((start + bar + 1.6) * RATE))
        tt = t[a:b] - start
        env = np.minimum(1.0, tt / 1.2) * np.clip((bar + 1.6 - tt) / 1.6, 0.0, 1.0)
        for name in chord:
            f = note(name)
            for side, detune in ((left, 0.9985), (right, 1.0015)):
                ph = rng.uniform(0, 2 * np.pi)
                v = sum(np.sin(2 * np.pi * f * h * detune * tt + ph * h) / h ** 1.6 for h in (1, 2, 3, 4))
                side[a:b] += 0.05 * env * v * (0.6 + 0.4 * np.sin(2 * np.pi * 0.11 * tt + ph))
        k += 1
        start += bar
    stereo = np.stack([left, right], axis=1)
    stereo *= 0.25 / np.max(np.abs(stereo))
    pcm = np.clip(stereo * 8388607, -8388607, 8388607).astype(np.int32)
    raw = pcm.reshape(-1).astype("<i4").view(np.uint8).reshape(-1, 4)[:, :3].tobytes()
    tmp = out + ".tmp.wav"
    with wave.open(tmp, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(3)
        w.setframerate(RATE)
        w.writeframes(raw)
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", tmp, "-af", "loudnorm=I=-24:TP=-6:LRA=7", "-ar", str(RATE),
                    "-c:a", "pcm_s24le", out], check=True)
    os.remove(tmp)


if __name__ == "__main__":
    main()
