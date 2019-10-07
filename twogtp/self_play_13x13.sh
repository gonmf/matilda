#!/bin/bash
rm -f output.html output.summary.dat
BLACK="../src/matilda     -m gtp -d ../src/data/ -l e --disable_opening_books --losing resign --playouts 10000"
WHITE="../src/matilda-old -m gtp -d ../src/data/ -l e --disable_opening_books --losing resign --playouts 10000"
REFEREE="gnugo --level 0 --mode gtp --chinese-rules --positional-superko"
gogui-twogtp -white "$WHITE" -black "$BLACK" -referee "$REFEREE" \
-sgffile output \
-games 4000 \
-auto \
-size 13 \
-komi 7.5 \
-alternate
exit 0
