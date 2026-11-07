# Place-and-route constraints. clk is over-constrained to CLOCK_PERIOD
# (20 ns) and SCLK_IN to 25 MHz, so the resizer keeps working towards a
# target above the 40 MHz / 20 MHz operating spec; signoff.sdc checks the spec.
set clk_period  $::env(CLOCK_PERIOD)
set sclk_period 40.0
source [file join [file dirname [info script]] af_timing.sdc]
