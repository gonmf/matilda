#!/bin/bash

cd twogtp
./clean.sh
cd ..

rm -rf twogtp1
rm -rf twogtp2
rm -rf twogtp3
rm -rf twogtp4
cp -r twogtp twogtp1
cp -r twogtp twogtp2
cp -r twogtp twogtp3
cp -r twogtp twogtp4

cd twogtp
nohup ./michi-c_19x19.sh &
cd ..

cd twogtp1
nohup ./michi-c_19x19.sh &
cd ..

cd twogtp2
nohup ./michi-c_19x19.sh &
cd ..

cd twogtp3
nohup ./michi-c_19x19.sh &
cd ..

cd twogtp4
nohup ./michi-c_19x19.sh &
cd ..

exit
