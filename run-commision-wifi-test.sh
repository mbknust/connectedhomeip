#!/bin/bash

systemctl stop bluetooth
rm -r /var/lib/bluetooth/*
REPEAT=1
ITERATIONS=1
PASSED=0

for i in $(seq 1 $REPEAT); do
   echo "Running test $i of $REPEAT"
   ./scripts/run_in_build_env.sh "\
         ./scripts/tests/run_test_suite.py \
         --runner chip_tool_python \
         --target TestCommissionerNodeId \
         --chip-tool ./out/linux-x64-chip-tool/chip-tool \
         run \
            --iterations $ITERATIONS \
            --test-timeout-seconds 120 \
            --all-clusters-app ./out/linux-x64-all-clusters/chip-all-clusters-app \
            --lock-app ./out/linux-x64-lock/chip-lock-app \
            --ota-provider-app ./out/linux-x64-ota-provider/chip-ota-provider-app \
            --ota-requestor-app ./out/linux-x64-ota-requestor/chip-ota-requestor-app \
            --tv-app ./out/linux-x64-tv-app/chip-tv-app \
            --bridge-app ./out/linux-x64-bridge/chip-bridge-app \
            --lit-icd-app ./out/linux-x64-lit-icd/lit-icd-app \
            --ble-wifi \
         "
   RESULT=$?
   echo "Command exited with status: $RESULT"
   if [ $RESULT -eq 0 ]; then
      echo "Test $i successful"
      PASSED=$((PASSED + 1))
   else
      echo "Test $i failed"
   fi
done
