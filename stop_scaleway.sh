#!/bin/bash
cd twogtp
./stop.sh
cd ..

cd twogtp
echo 'Results instance 0'
./analyze_short.sh
cd ..

cd twogtp1
echo 'Results instance 1'
./analyze_short.sh
cd ..

cd twogtp2
echo 'Results instance 2'
./analyze_short.sh
cd ..

cd twogtp3
echo 'Results instance 3'
./analyze_short.sh
cd ..

cd twogtp4
echo 'Results instance 4'
./analyze_short.sh
cd ..

cd twogtp5
echo 'Results instance 5'
./analyze_short.sh
cd ..

cd twogtp6
echo 'Results instance 6'
./analyze_short.sh
cd ..

exit
