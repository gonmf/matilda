#!/bin/bash
rm -f output.html output.summary.dat
gogui-twogtp -black "../src/matilda -m gtp --disable_score_estimation -d ../src/data/ -l" -white "python ../../michi/michi.py gtp" -komi 7.5 -auto -sgffile output -games 3000 -size 9
exit 0
