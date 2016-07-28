#!/bin/bash
rm -f output.html output.summary.dat
gogui-twogtp -white "gnugo --mode gtp --chinese-rules --positional-superko --resign-allowed" -black "../src/matilda -m gtp --think_in_opt_time -d ../src/data/ -l" -size 9 -komi 7.5 -auto -sgffile output -games 4000 -alternate -time 5m
exit 0
