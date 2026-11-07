# Adaptive Filter Explorer v0.2: float and the chip's fixed point

A local copy of `~/dev/afx/appv0p1` (C++20, Dear ImGui, SDL2), extended to run the adaptive filter either in
floating point (as before) or on the **chip's own arithmetic**: the 4-tap sign-sign LMS datapath of
`tt_um_adaptive_filter` (assess4d v5), bit for bit. A testbench checks that the app's fixed point matches the
Python golden model, the RTL and the routed netlist exactly.

## Build and run

    D=~/dev/vsyn/appv0p3/build/_deps        # imgui, json, stb already fetched (offline build)
    cmake -S . -B build -G Ninja -DFETCHCONTENT_SOURCE_DIR_IMGUI=$D/imgui-src \
          -DFETCHCONTENT_SOURCE_DIR_JSON=$D/json-src -DFETCHCONTENT_SOURCE_DIR_STB=$D/stb-src \
          -DFETCHCONTENT_FULLY_DISCONNECTED=ON
    ninja -C build
    ./build/AdaptiveFilterExplorer           # the window
    ./build/afx_selftest                     # headless checks of the core, the chip model included
    ./build/afx_shot shots                   # screenshots, off-screen (no display needed)

## The chip in the app

- **Chip scene** (toolbar) loads the default fetal-ECG extraction on the chip's arithmetic. **C** switches any
  scene between float and chip. The *arithmetic* switch is also at the top of the filter inspector.
- **What chip mode does:**
  - x and d (mV) are quantized to Q1.7 codes at a full scale FS (code ±128).
  - `ChipCore::step` runs the RTL's arithmetic on the codes.
  - y and e come back from their codes.
- **Fixed by the silicon:** 4 taps and sign-sign LMS. Leakage and the other algorithms don't apply.
- **Controls:**
  - full scale FS (mV);
  - `mu_sel` 0 to 7, giving μ = 2^(mu_sel − 12) (uio[3:1]);
  - `adapt_en` (uio[0], the same as Freeze).
- **Live chip state:**
  - the last u/r/y/e codes;
  - `debug_o2` (the sticky saturation flag);
  - how many inputs hit the rails;
  - the weight registers, and the bytes the chip's readout would send (y lane w[k] >> 6, e lane x[k]).
- **Float twin:** the same 4-tap sign-sign LMS and μ in floating point runs beside the chip on the unquantized
  signals. The learning curve draws both, so the gap is what the word lengths cost.
  - On the default scene at FS 1.5 mV, mu_sel 1: about +25.4 dB SNR improvement for the chip against +26.1 dB
    for its twin.
  - Lowering FS to 0.5 mV clips the inputs, sets `debug_o2` and opens the gap (`shots/chip-clipping.png`).
- The chip runs at 1.54 MS/s on silicon (40 MHz clk, 13 SCLK_IN periods per sample). The app feeds it the
  scene's 500 S/s, which changes nothing in the arithmetic.
- Settings files keep `arithmetic`, `fullScale` and `muSel` in the filter section (still format "afx 1").

## The model: `Source/Chip.h`, `Source/Chip.cpp`

| Quantity | Format |
|---|---|
| u, r, y, e | 8-bit Q1.7 |
| w[k] | 14-bit Q2.12 register; the FIR uses w >> 4 (Q2.8) |

- `y = sat8((Σ x[k]·(w[k] >> 4) + 2^7) >> 8)`, which rounds half up.
- `e = sat8(r − y)`.
- `w[k] = sat14(w[k] + 2^mu_sel · sign(r − y) · sign(x[k]))`, using the unsaturated r − y, with sign(0) = 0.
- The sticky flag sets if y, e or a weight update saturates. Input clipping is not flagged by the chip; the
  app counts it separately.

## The testbench: `tb/chip_tb.py`

    python3 tb/chip_tb.py                          # RTL (cocotb + Icarus, test/test.py)
    GL=1 python3 tb/chip_tb.py                     # routed netlist runs/r18, zero delay
    SDF=max_ss_125C_3v00 python3 tb/chip_tb.py     # routed netlist with SDF timing (slow corner)
    SDF=min_ff_n40C_3v60 python3 tb/chip_tb.py     # (fast corner)

- **Vectors:** `build/afx_chipvec` writes them from the app itself. For the `app_*` cases it runs the
  explorer's `Simulator` in chip mode, so the expected frames are what the GUI computes.

  | Case | Stimulus |
  |---|---|
  | `app_fetal` | The default scene, 12,000 samples. μ steps, freezes and 8 weight readouts on the way. |
  | `app_hum` | A sine under mains hum through an echo, at FS 0.4 mV: inputs clip, y/e saturate, `debug_o2` sets. |
  | `corners` | Full-range random codes at every mu_sel, adapt_en toggles, every pair of rail values, weights driven into saturation. |

- **Checks:** every frame (each y/e frame, each readout, the flags) is checked two ways:
  - C++ model vs `../lms_model.py`;
  - C++ model vs the chip simulated through the cocotb testbench (`test/test.py`, `test_vectors`) at the operating
    spec: clk 40 MHz, SCLK_IN 20 MHz, 13 periods per sample.
- **Proving the check can fail:** `CHIPVEC=<other build>` runs a different vector tool. Deliberately broken
  models (rounding off by one, sign(0) = +1) fail at the first wrong frame.
- **Output:** logs go to `tb/logs/` and simulation files to `tb/work/<level>/cocotb/`.

## Files changed from appv0p1

- **New:**
  - `Source/Chip.h`, `Source/Chip.cpp` (the model);
  - `Source/ChipVectors.cpp` (`afx_chipvec`);
  - `tb/chip_tb.py`;
  - this README.
- **Extended:**
  - `Adaptive.*`: the arithmetic switch;
  - `Simulator.*`: the twin and the clip count;
  - `Scene.*`: `Scene::chip()`;
  - `Project.cpp`: JSON;
  - `Panels.cpp`: the chip inspector;
  - `Ui.*`: the Chip scene button, the C key and the twin in the learning curve;
  - `Diagram.cpp`, `SelfTest.cpp` (chip checks), `Shot.cpp` (three chip shots), `CMakeLists.txt`.
- **Not copied:** the 132 MB promo video and its frames. The scripts are in `promo/`.
