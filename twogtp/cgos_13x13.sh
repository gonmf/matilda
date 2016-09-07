#!/bin/bash
rm -f output.html output.summary.dat
BLACK="../src/matilda -m gtp -d ../src/data/ -l --think_in_opt_time"
WHITE="gnugo --level 10 --mode gtp --chinese-rules --positional-superko --resign-allowed"
REFEREE="gnugo --level 10 --mode gtp --chinese-rules --positional-superko"
gogui-twogtp -white "$WHITE" -black "$BLACK" -referee "$REFEREE" -auto -sgffile output -games 4000 \
-size 13 \
-komi 7.5 \
-alternate \
-time 10m
exit 0
