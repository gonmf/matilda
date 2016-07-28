#!/bin/bash
rm -f output.html output.summary.dat
gogui-twogtp -white "gnugo --level 0 --mode gtp --chinese-rules --positional-superko --resign-allowed" -black "../src/matilda -m gtp --disable_score_estimation -d ../src/data/ -l" -size 13 -komi 7.5 -auto -sgffile output -games 10000 -alternate
exit 0
