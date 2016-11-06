#!/bin/bash
rm -f output.html output.summary.dat
BLACK="../src/matilda -m gtp -d ../src/data/ -l --disable_opening_books"
WHITE="../src/matilda-old -m gtp -d ../src/data/ -l --disable_opening_books"
REFEREE="gnugo --level 0 --mode gtp --chinese-rules --positional-superko"
gogui-twogtp -white "$WHITE" -black "$BLACK" -referee "$REFEREE" \
-sgffile output \
-games 4000 \
-auto \
-size 13 \
-komi 7.5 \
-alternate \
-time 8m
exit 0
