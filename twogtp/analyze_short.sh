#!/bin/bash
gogui-twogtp -analyze ./output.dat
head -47 output.html | grep 'Black wins'
head -36 output.html | grep 'Games used'
exit 0
