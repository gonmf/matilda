#!/bin/bash
rm -f output.html output.summary.dat
SIMS="20000"
BLACK="../src/matilda -m gtp -d ../src/data/ -l e --disable_opening_books --losing resign --disable_neural_nets --threads 1 --playouts $SIMS"
WHITE="../src/michi-c $SIMS"
REFEREE="gnugo --level 0 --mode gtp --chinese-rules --positional-superko"
gogui-twogtp -white "$WHITE" -black "$BLACK" -referee "$REFEREE" \
-sgffile output \
-games 2000 \
-auto \
-size 19 \
-komi 7.5 \
-alternate
exit 0
