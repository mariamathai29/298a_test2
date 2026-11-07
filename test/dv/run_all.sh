#!/bin/sh
# Full DV regression on run $RUN: RTL, zero-delay gate level, and SDF timing
# simulation at the slow and fast signoff corners (one process per case).
# Each is one cocotb run of test/Makefile with its own build directory.
set -u
T=$(cd "$(dirname "$0")/.." && pwd)
R=$(readlink -f "$T/../runs/$RUN/final")
export PDK_ROOT=${PDK_ROOT:-$HOME/dataa/tt/pdk}
O=$T/dv/$RUN
mkdir -p $O

# run <name> [make args]: log to $O/<name>.log, ending in a RESULT line
run() {
  n=$1; shift
  ( make -C $T SIM_BUILD=$O/$n COCOTB_RESULTS_FILE=$O/$n/results.xml "$@" \
      && echo "RESULT: PASS" || echo "RESULT: FAIL" ) > $O/$n.log 2>&1
}

run rtl &
run gl GATES=yes GL=$R/pnl/tt_um_adaptive_filter.pnl.v &
for c in max_ss_125C_3v00 min_ff_n40C_3v60; do
  for k in main sysid tight async overrun; do
    run sdf_${c}_$k GATES=yes GL=$R/nl/tt_um_adaptive_filter.nl.v \
        SDF=$R/sdf/$c/tt_um_adaptive_filter__$c.sdf COCOTB_TEST_FILTER=test_$k &
  done
done
wait
grep -H "RESULT" $O/*.log
