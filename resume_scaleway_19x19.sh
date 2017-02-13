#!/bin/bash

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
