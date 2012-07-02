#!/bin/bash

hostname=`hostname -s`

case ${hostname} in

  asa1) \
    instances=(
      #                     GPU     NET FLF GPU OUT
      # mask  bind_host     DEV XID CPU CPU CPU CPU
      '0x0707 192.168.2.41   0   1   2   8   1   8' # Instance 0, bond0 (eth2+eth3)
      '0x7070 192.168.2.42   1   5   6  12   5  12' # Instance 1, bond1 (eth4+eth5)
    );;

  paper5) \
    instances=( 
      #                     GPU     NET FLF GPU OUT
      # mask  bind_host     DEV XID CPU CPU CPU CPU
      '0x0606 192.168.2.5    0   2   2   4   3   4' # Instance 0, eth2
    );;
esac

function init() {
  instance=$1
  mask=$2
  bindhost=$3
  gpudev=$4
  xid=$5
  netcpu=$6
  flfcpu=$7
  gpucpu=$8
  outcpu=$9

  echo taskset $mask \
  ./paper_xgpu -I $instance \
    -o BINDHOST=$bindhost \
    -o GPUDEV=$gpudev \
    -o XID=$xid \
    -c $netcpu paper_net_thread \
    -c $flfcpu paper_fluff_thread \
    -c $gpucpu paper_gpu_thread \
    -c $outcpu paper_gpu_output_thread

  taskset $mask \
  ./paper_xgpu -I $instance \
    -o BINDHOST=$bindhost \
    -o GPUDEV=$gpudev \
    -o XID=$xid \
    -c $netcpu paper_net_thread \
    -c $flfcpu paper_fluff_thread \
    -c $gpucpu paper_gpu_thread \
    -c $outcpu paper_gpu_output_thread \
    2> out$instance
}


while [ -n "$1" ]
do
  args="${instances[$1]}"
  if [ -n "${args}" ]
  then
    echo
    echo Starting instance $1
    init $1 $args &
    echo Instance $1 pid $!
  else
    echo Instance $1 not defined for host $hostname
  fi
  shift
  if [ -n "$1" ]
  then
    sleep 10
  fi
done
