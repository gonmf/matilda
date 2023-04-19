#!/bin/bash
../src/matilda-twogtp \
  --black "../src/matilda -l ew --save_all -m gtp -d ../src/data/ --disable_opening_books --losing resign" \
  --white "../src/matilda2 -l ew -m gtp -d ../src/data/ --disable_opening_books --losing resign" \
  --referee "gnugo --mode gtp --chinese-rules --positional-superko" \
  --size 19 \
  --komi 7.5 \
  --games 50 \
  --alternate
exit 0
