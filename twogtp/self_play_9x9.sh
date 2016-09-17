#!/bin/bash
rm -f output.html output.summary.dat
BLACK="../src/matilda -m gtp -d ../src/data/ -l --disable_opening_books --playouts 1000"
WHITE="../src/matilda-old -m gtp -d ../src/data/ -l --disable_opening_books --playouts 1000"
REFEREE="gnugo --level 0 --mode gtp --chinese-rules --positional-superko"
gogui-twogtp -white "$WHITE" -black "$BLACK" -referee "$REFEREE" -auto -sgffile output -games 4000 \
-size 9 \
-komi 7.5 \
-alternate
exit 0
