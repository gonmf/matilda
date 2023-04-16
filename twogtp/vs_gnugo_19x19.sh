#!/bin/bash
../src/matilda-twogtp \
  --black "../src/matilda -l ew --save_all -m gtp -d ../src/data/ --disable_opening_books --playouts 1000 --losing resign" \
  --white "gnugo --level 0 --mode gtp --chinese-rules --positional-superko --resign-allowed" \
  --referee "gnugo --mode gtp --chinese-rules --positional-superko" \
  --size 19 \
  --komi 7.5 \
  --games 10 \
  --alternate
exit 0
