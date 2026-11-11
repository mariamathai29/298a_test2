<!---

This file is used to generate your project datasheet. Please fill in the information below and delete any unused
sections.

You can also include images in this folder and reference them in the markdown. Each image must be less than
512 kb in size, and the combined size of all images must be less than 1 MB.
-->

## How it works

A 4-tap adaptive FIR filter whose weights are updated by a sign-sign LMS engine. The nominal use is
interference cancellation: the reference `u` feeds the FIR, the desired input `r` carries signal plus
correlated interference, and the chip outputs `y` and the error `e = r - y` (the cleaned signal). The
same structure identifies an unknown 4-tap FIR when `r` is that system's response to `u`.

- Samples `u`, `r` and outputs `y`, `e` are signed Q1.7. Weights are 14-bit Q2.12 registers; the FIR
  uses the top 10 bits (Q2.8).
- Update: `w[k] += mu * sign(e) * sign(x[k])`, with `mu = 2^(mu_sel - 12)` (`mu_sel` 0 to 7).
- Input port: SPI slave (mode 0, LSB first, `CS_IN` active low) on its own clock `SCLK_IN`. `u` and `r`
  shift in on two lanes; after the 8th bit both words move into holding registers, and a toggle
  synchroniser starts one filter step in the `clk` domain.
- One step takes 23 `clk` cycles (17-cycle radix-4 Booth multiply-accumulate, 1 finish, 4 update).
- Output port: synchronous serial master. The chip drives `SCLK_OUT = clk / 2` and `FS_OUT` (low for
  the whole frame, high between frames); `y`, `e` and one weight leave together, LSB first, on
  three lanes as one 8-bit frame per sample.
- Weight monitor: frame `n` (counted from reset) carries weight `n mod 4` on `MOSI_w_out`, top 8 bits
  (Q2.6), as it was before that sample's update. A full set arrives every 4 frames, live while
  adapting; with `adapt_en = 0`, 4 consecutive frames are an exact snapshot.

Operating spec: `clk` 40 MHz, `SCLK_IN` 20 MHz, `SCLK_OUT` 20 MHz, 13 `SCLK_IN` periods per sample
(1.54 MS/s). Timing is met at all nine signoff corners at this spec (built against 50 MHz).

## How to test

Drive `clk` at 40 MHz. Hold `rst_n` low with `CS_IN` high, then release it. Set `uio[0] = 1`
(`adapt_en`) and `uio[3:1] = mu_sel` (4 is a good start). For each sample, with `CS_IN` low, send 8
bits LSB first on `MOSI_u_in` and `MOSI_r_in` at 20 MHz (change data on falling `SCLK_IN`), then raise
`CS_IN` for at least 5 more `SCLK_IN` periods. Successive samples must be at least 23 `clk` cycles
apart. Capture `MOSI_y_out`, `MOSI_e_out` and `MOSI_w_out` on rising `SCLK_OUT` while `FS_OUT` is low,
LSB first.

To track the weights, count frames from reset: frame `n` carries weight `n mod 4`. A dropped sample
produces no frame, so the count stays in step. `debug_o1` is a sticky flag for a dropped sample (samples too close together), and `debug_o2` is a
sticky flag for saturation. Both clear on reset.

| Pin | Function |
|-----|----------|
| `ui[0]` | `SCLK_IN` |
| `ui[1]` | `CS_IN` (active low) |
| `ui[2]` | `MOSI_u_in` |
| `ui[4]` | `MOSI_r_in` |
| `uo[0]` | `SCLK_OUT` |
| `uo[1]` | `FS_OUT` |
| `uo[2]` | `MOSI_y_out` |
| `uo[3]` | `MOSI_e_out` |
| `uo[4]` | `MOSI_w_out` (monitor weight) |
| `uo[6]` | `debug_o1` (overrun) |
| `uo[7]` | `debug_o2` (saturation) |
| `uio[0]` | `adapt_en` (input) |
| `uio[3:1]` | `mu_sel` (input) |

## External hardware

An SPI master for the input port and a 20 MHz frame-sync serial receiver for the output port (FPGA,
or an MCU SAI/SSP in TI-SSI mode).
