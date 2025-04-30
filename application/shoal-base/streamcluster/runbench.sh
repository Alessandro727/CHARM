#!/bin/bash

#for i in 1 2 4 8 16 24 32 40 48 56 64
#72 80 88 96 104 112 120 128
#for i in 64 56 48 40 32 24 16 8 4 2 1
for i in 64
do
#echo "Cores: ${i}" &>> res_baseline_last.txt
echo "Cores: ${i}" &>> test.txt
{ time LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../libshoal/shoal/ ./streamcluster 10 20 128 1000000 200000 5000 none output.txt ${i}; } &>> test.txt

#{ time LD_PRELOAD=/home/fogli/mimalloc-bench/extern/je/lib/libjemalloc.so ./streamcluster 10 20 128 1000000 200000 5000 none output.txt ${i}; } &>> res_jemalloc.txt
echo ' ' &>> test.txt
done
