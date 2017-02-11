#!/bin/bash
rm -f output.html output.summary.dat
BLACK="../src/matilda -m gtp -d ../src/data/ -l e --disable_opening_books --losing resign --disable_neural_nets --playouts 10000 --threads 1"
WHITE="../../michi-c/michi gtp"
REFEREE="gnugo --level 0 --mode gtp --chinese-rules --positional-superko"
gogui-twogtp -white "$WHITE" -black "$BLACK" -referee "$REFEREE" \
-sgffile output \
-games 4000 \
-auto \
-size 19 \
-komi 7.5 \
-alternate
exit 0
