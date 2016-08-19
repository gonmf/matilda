#!/bin/bash
rm -f output.html output.summary.dat
BLACK="../src/matilda -m gtp -d ../src/data/ -l"
WHITE="gnugo --level 0 --mode gtp --chinese-rules --positional-superko --resign-allowed"
REFEREE="gnugo --level 0 --mode gtp --chinese-rules --positional-superko"
gogui-twogtp -white "$WHITE" -black "$BLACK" -size 9 -komi 7.5 -auto -sgffile output -games 4000 -alternate -referee "$REFEREE"
exit 0
