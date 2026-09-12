#!/bin/bash
../src/matilda-twogtp \
  --black "../src/matilda -l ew --save_all -m gtp -d ../src/data/ --disable_opening_books --playouts 1400 --losing resign" \
  --white "../../michi-c/michi gtp" \
  --referee "gnugo --mode gtp --chinese-rules --positional-superko" \
  --size 13 \
  --komi 7.5 \
  --games 50 \
  --alternate
exit 0
