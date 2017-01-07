#!/bin/bash
gogui-twogtp -analyze ./output.dat
cat output.html | grep -E 'Black wins|<h2>Result'
cat output.html | grep "Games:" -F
cat output.html | grep "Games used:" -F
cat output.html | grep ">Time Black:" -F
cat output.html | grep ">Time White:" -F
exit 0
