#!/bin/bash
gogui-twogtp -analyze ./output.dat
echo "WR for matilda (black):"
cat output.html | grep "Black wins:" -F
cat output.html | grep "Games:" -F
cat output.html | grep "Games used:" -F
cat output.html | grep ">Time Black:" -F
cat output.html | grep ">Time White:" -F
exit 0
