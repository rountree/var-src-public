#!/bin/bash

# iris, Intel(R) Xeon(R) CPU E5-2630 v3 @ 2.40GHz, dfdm=0x063F
# Two socket, second-socket CPUs are 8-15, 24-31

#./var -m 0 \
#    --longitudinal=FIXED_FUNCTION_COUNTERS:8-15,24-31 \
#    --longitudinal=ALL_ALLOWED:1,9 \
#    --time=10s

./var -m 0 \
    --benchmark=ABSHIFT:9-14:hw30:hw1:1 \
    --poll=0x611:OP_POLL+OP_TSC+DELTA_TSC+OP_THERM+OP_PTHERM+DELTA_THERM+DELTA_PTHERM:500us:2:8 \
    --time=3h \
    --abTime=100ms



