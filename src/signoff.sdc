# Signoff constraints at the operating spec: clk 40 MHz, SCLK_IN 20 MHz
# (SCLK_OUT = clk / 2 = 20 MHz). See af_timing.sdc for the I/O model.
set clk_period  25.0
set sclk_period 50.0
source [file join [file dirname [info script]] af_timing.sdc]
