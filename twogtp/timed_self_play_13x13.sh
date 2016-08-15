#!/bin/bash
rm -f output.html output.summary.dat
gogui-twogtp -black "../src/matilda -m gtp --disable_opening_books -d ../src/data/ -l" -white "../src/matilda13 -m gtp --disable_opening_books --disable_score_estimation -d ../src/data/ -l" -komi 7.5 -auto -sgffile output -games 2000 -size 13 -alternate -time 9s
exit 0
