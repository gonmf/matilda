#!/bin/bash
rm -f output.html output.summary.dat
BLACK="../src/matilda -m gtp -d ../src/data/ -l e --disable_opening_books --losing resign --disable_neural_nets --threads 1 --playouts 10000"
WHITE="../src/michi-c gtp"
REFEREE="gnugo --level 0 --mode gtp --chinese-rules --positional-superko"
gogui-twogtp -white "$WHITE" -black "$BLACK" -referee "$REFEREE" \
-sgffile output \
-games 4000 \
-auto \
-size 9 \
-komi 5.5 \
-alternate
exit 0
