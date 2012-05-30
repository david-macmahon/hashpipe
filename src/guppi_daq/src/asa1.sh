#!/bin/bash

function inst0() {
  taskset 0x0606 \
  ./paper_xgpu -I 0 \
    -o BINDHOST=192.168.2.41 \
    -o GPUDEV=0 \
    -o XID=1 \
    -c 1 paper_net_thread \
    -c 8 paper_gpu_thread \
         paper_gpu_output_thread \
    2> out0
}

function inst1() {
  taskset 0x0606 \
  ./paper_xgpu -I 1 \
    -o BINDHOST=192.168.2.42 \
    -o GPUDEV=0 \
    -o XID=3 \
    -c 2 paper_net_thread \
    -c 8 paper_gpu_thread \
         paper_gpu_output_thread \
    2> out1
}

function inst2() {
  taskset 0x6060 \
  ./paper_xgpu -I 2 \
    -o BINDHOST=192.168.2.141 \
    -o GPUDEV=1 \
    -o XID=5 \
    -c 5 paper_net_thread \
    -c 4 paper_gpu_thread \
         paper_gpu_output_thread \
    2> out2
}

function inst3() {
  taskset 0x6060 \
  ./paper_xgpu -I 3 \
    -o BINDHOST=192.168.2.142 \
    -o GPUDEV=1 \
    -o XID=0 \
    -c 6 paper_net_thread \
    -c 4 paper_gpu_thread \
         paper_gpu_output_thread \
    2> out3
}

while [ -n "$1" ]
do
  echo
  echo Starting instance $1
  inst$1 &
  shift
  if [ -n "$1" ]
  then
    sleep 10
  fi
done
