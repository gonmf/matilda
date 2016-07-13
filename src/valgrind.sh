#!/bin/bash
valgrind --tool=callgrind --dump-instr=yes --simulate-cache=yes \
    --collect-jumps=yes ./matilda -gtp -strategy mcts
kcachegrind
exit 0

