#!/bin/bash
rm -f output.html output.summary.dat
BLACK="../src/matilda -m gtp -d ../src/data/ -l --disable_opening_books --memory 3200"
WHITE="../src/matilda-old -m gtp -d ../src/data/ -l --disable_opening_books --memory 3200"
REFEREE="gnugo --level 0 --mode gtp --chinese-rules --positional-superko"
gogui-twogtp -white "$WHITE" -black "$BLACK" -referee "$REFEREE" \
-sgffile output \
-games 4000 \
-auto \
-size 9 \
-komi 7.0 \
-alternate \
-time 1m
exit 0
