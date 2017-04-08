#!/bin/bash
echo 'Cleaning...'
cd twogtp
./clean.sh
cd ..

rm -rf twogtp1
rm -rf twogtp2
rm -rf twogtp3
rm -rf twogtp4
rm -rf twogtp5

cp -r twogtp twogtp1
cp -r twogtp twogtp2
cp -r twogtp twogtp3
cp -r twogtp twogtp4
cp -r twogtp twogtp5

echo 'Starting...'
./resume_scaleway_13x13.sh

exit 0
