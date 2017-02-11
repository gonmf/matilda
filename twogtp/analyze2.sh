#!/bin/bash
gogui-twogtp -analyze ./output.dat
cat output.html | grep -E 'Black wins|<h2>Result'
cat output.html | grep "Games used:" -F
exit 0
