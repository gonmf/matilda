#!/bin/bash

cd twogtp
./stop.sh
echo '----------- part 0 -----------'
./analyze2.sh
cd ..

cd twogtp1
echo '----------- part 1 -----------'
./analyze2.sh
cd ..

cd twogtp2
echo '----------- part 2 -----------'
./analyze2.sh
cd ..

cd twogtp3
echo '----------- part 3 -----------'
./analyze2.sh
cd ..

cd twogtp4
echo '----------- part 4 -----------'
./analyze2.sh
cd ..

cd twogtp5
echo '----------- part 5 -----------' 
./analyze2.sh
cd .. 

exit
