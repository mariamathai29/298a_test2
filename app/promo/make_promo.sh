#!/usr/bin/env bash
# Rebuilds the Adaptive Filter Explorer promo from nothing but the sources:
#   the picture and the sonification (afx_promo: the real interface off-screen, scripted),
#   the bed (the ASyn bed if there is one, else a synthesized pad), the mix, the video,
#   the subtitles and the contact sheet.  Deterministic: the same input, the same output.
#     promo/make_promo.sh             everything
#     promo/make_promo.sh --mix-only  just the bed, the mix and the mux (after swapping bed.wav or the ASyn bed)
# Environment: AFX_BED=path/to/bed.wav to use another bed.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app="$(dirname "$here")"
build="$app/build"
bed_src="${AFX_BED:-$HOME/dev/promo/beds/afx_bed.wav}"
ff=(ffmpeg -hide_banner -loglevel error -y)

if [[ "${1:-}" != "--mix-only" ]]; then
    echo "== building"
    cmake --build "$build" --target afx_promo afx_selftest
    "$build/afx_selftest" > /dev/null || { echo "afx_selftest failed" >&2; exit 1; }

    echo "== the picture and the sonification (a while: the software renderer, 1800 frames)"
    "$build/afx_promo" "$here" \
        | "${ff[@]}" -f rawvideo -pix_fmt rgba -s 1920x1080 -r 30 -i - \
            -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p -r 30 "$here/.video.mp4"
fi

echo "== the bed"
if [[ -f "$bed_src" ]]; then
    "${ff[@]}" -i "$bed_src" -t 60 -ar 48000 -ac 2 -c:a pcm_s24le "$here/bed.wav"
    echo "   from $bed_src"
elif [[ ! -f "$here/bed.wav" || "${1:-}" != "--mix-only" ]]; then
    python3 "$here/make_pad.py" "$here/bed.wav"
    echo "   synthesized (no ASyn bed found)"
fi

echo "== the mix"
python3 "$here/mix.py" "$here"

echo "== the video"
"${ff[@]}" -i "$here/.video.mp4" -i "$here/afx_soundtrack.wav" -map 0:v:0 -map 1:a:0 \
    -c:v copy -c:a aac -b:a 192k -ar 48000 -ac 2 -t 60 -movflags +faststart "$here/afx_promo.mp4"

echo "== the contact sheet"
"${ff[@]}" -start_number 1 -i "$here/frames/scene_%d.png" \
    -vf "scale=640:360:flags=area,tile=3x3:margin=12:padding=12:color=0x0e1013" -frames:v 1 "$here/contact_sheet.png"

ffprobe -v error -show_entries format=duration -of default=nw=1 "$here/afx_promo.mp4"
echo "done: $here/afx_promo.mp4"
