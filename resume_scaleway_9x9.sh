#!/bin/bash

cd twogtp
nohup ./michi-c_9x9.sh &
cd ..

sleep 1

cd twogtp1
nohup ./michi-c_9x9.sh &
cd ..

sleep 1

cd twogtp2
nohup ./michi-c_9x9.sh &
cd ..

sleep 1

cd twogtp3
nohup ./michi-c_9x9.sh &
cd ..

sleep 1

cd twogtp4
nohup ./michi-c_9x9.sh &
cd ..

sleep 1

cd twogtp5
nohup ./michi-c_9x9.sh &
cd ..

exit

