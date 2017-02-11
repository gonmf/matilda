#!/bin/bash

cd twogtp
./clean.sh
cd ..

mv -rf twogtp1
mv -rf twogtp2
mv -rf twogtp3
mv -rf twogtp4
cp -r twogtp twogtp1
cp -r twogtp twogtp2
cp -r twogtp twogtp3
cp -r twogtp twogtp4

cd twogtp
nohup ./michi-c_9x9.sh &
cd ..

cd twogtp1
nohup ./michi-c_9x9.sh &
cd ..

cd twogtp2
nohup ./michi-c_9x9.sh &
cd ..

cd twogtp3
nohup ./michi-c_9x9.sh &
cd ..

cd twogtp4
nohup ./michi-c_9x9.sh &
cd ..

exit
