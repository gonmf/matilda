#!/bin/bash
rm -f output.html output.summary.dat
BLACK="../src/matilda     -m gtp -d ../src/data/ -l e --disable_opening_books --disable_neural_nets --losing resign --memory 3200"
WHITE="../src/matilda-old -m gtp -d ../src/data/ -l e --disable_opening_books --disable_neural_nets --losing resign --memory 3200"
REFEREE="gnugo --level 0 --mode gtp --chinese-rules --positional-superko"
gogui-twogtp -white "$WHITE" -black "$BLACK" -referee "$REFEREE" \
-sgffile output \
-games 4000 \
-auto \
-size 9 \
-komi 7.0 \
-alternate \
-time 5m
exit 0
