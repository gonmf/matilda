#!/bin/bash
valgrind --tool=callgrind --dump-instr=yes --simulate-cache=yes \
    --collect-jumps=yes ./matilda --benchmark
    # --disable_opening_books -l
kcachegrind
exit 0
