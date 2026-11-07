#!/usr/bin/env python3
"""The promo's soundtrack: the sonification over the music bed, the bed ducked
under the heartbeats, then levelled to -16 LUFS integrated with true peaks at or
below -1 dBTP.  Swap the bed by replacing bed.wav and running this again.

    mix.py [promo_dir]     reads sonification.wav and bed.wav, writes afx_soundtrack.wav
"""

import json
import os
import subprocess
import sys

DURATION = 60.0
TARGET_I = -16.0
LIMIT_DB = -1.6      # sample-peak ceiling of the limiter (true peaks land a little above)
BED_GAIN_DB = -2.0   # the bed against the sonification, before ducking


def ffmpeg(*args):
    return subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-y", *args], check=True, capture_output=True, text=True)


def loudness(path):
    """Integrated loudness and true peak, by ffmpeg's loudnorm analysis."""
    out = ffmpeg("-i", path, "-af", "loudnorm=I=-16:TP=-1:print_format=json", "-f", "null", "-").stderr
    data = json.loads(out[out.rindex("{"):out.rindex("}") + 1])
    return float(data["input_i"]), float(data["input_tp"])


def main():
    here = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(__file__))
    son, bed = os.path.join(here, "sonification.wav"), os.path.join(here, "bed.wav")
    pre, out = os.path.join(here, ".premix.wav"), os.path.join(here, "afx_soundtrack.wav")
    # 1. the bed: in over the title, out with the end card, ducked a few dB by the thumps
    graph = (
        f"[1:a]aformat=sample_rates=48000:channel_layouts=stereo,atrim=0:{DURATION},asetpts=N/SR/TB,"
        f"afade=t=in:st=0:d=2.5,afade=t=out:st={DURATION - 3.6}:d=3.5,volume={BED_GAIN_DB}dB[bed];"
        "[0:a]aformat=sample_rates=48000:channel_layouts=stereo,asplit=2[son][key];"
        "[bed][key]sidechaincompress=threshold=0.12:ratio=1.8:attack=20:release=500:knee=4[ducked];"
        f"[son][ducked]amix=inputs=2:normalize=0:duration=first,atrim=0:{DURATION}[mix]"
    )
    ffmpeg("-i", son, "-i", bed, "-filter_complex", graph, "-map", "[mix]", "-c:a", "pcm_f32le", pre)
    i0, tp0 = loudness(pre)
    # 2. level and limit; once more if the limiter moved the loudness
    gain = TARGET_I - i0
    for _ in range(3):
        limit = 10 ** (LIMIT_DB / 20)
        ffmpeg("-i", pre, "-af",
               f"volume={gain:.3f}dB,alimiter=limit={limit:.4f}:attack=2:release=60:level=0:asc=1,"
               f"atrim=0:{DURATION}",
               "-ar", "48000", "-ac", "2", "-c:a", "pcm_s24le", out)
        i1, tp1 = loudness(out)
        if abs(i1 - TARGET_I) <= 0.2:
            break
        gain += TARGET_I - i1
    os.remove(pre)
    print(f"premix {i0:.1f} LUFS ({tp0:.1f} dBTP); soundtrack {i1:.1f} LUFS, {tp1:.1f} dBTP (gain {gain:+.1f} dB)")


if __name__ == "__main__":
    main()
