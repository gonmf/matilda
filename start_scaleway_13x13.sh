#!/bin/bash
cd matilda

cd twogtp0
./clean.sh
nohup ./michi-c_13x13.sh &
cd ..

cd twogtp1
./clean.sh
nohup ./michi-c_13x13.sh &
cd ..

cd twogtp2
./clean.sh
nohup ./michi-c_13x13.sh &
cd ..

cd twogtp3
./clean.sh
nohup ./michi-c_13x13.sh &
cd ..

cd twogtp4
./clean.sh
nohup ./michi-c_13x13.sh &
cd ..

exit
