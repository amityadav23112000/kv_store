#!/bin/bash
# Run the direct-store benchmark for all three store variants.
for variant in v1_single v2_sharded v3_sharded_shared; do
    cp "kvstore_${variant}.h" kvstore.h
    g++ -std=c++17 -O2 -Wall -Wextra -o bench_store bench_store.cpp
    echo "=== ${variant} ==="
    ./bench_store 8 200000
    ./bench_store 8 200000
    ./bench_store 8 200000
done
cp kvstore_v3_sharded_shared.h kvstore.h    # leave the project on V3
cp "kvstore_${variant}.h" kvstore.h || { echo "MISSING ${variant} - aborting"; exit 1; }