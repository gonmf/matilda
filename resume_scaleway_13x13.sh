#!/bin/bash
echo 'Instance 0'
cd twogtp
nohup ./michi-c_13x13.sh &
cd ..

sleep 1

echo 'Instance 1'
cd twogtp1
nohup ./michi-c_13x13.sh &
cd ..

sleep 1

echo 'Instance 2'
cd twogtp2
nohup ./michi-c_13x13.sh &
cd ..

sleep 1

echo 'Instance 3'
cd twogtp3
nohup ./michi-c_13x13.sh &
cd ..

sleep 1

echo 'Instance 4'
cd twogtp4
nohup ./michi-c_13x13.sh &
cd ..

sleep 1

echo 'Instance 5'
cd twogtp5
nohup ./michi-c_13x13.sh &
cd ..

sleep 1

echo 'Instance 6'
cd twogtp5
nohup ./michi-c_13x13.sh &
cd ..

exit 0

