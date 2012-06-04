#!/bin/bash

hostname=`hostname -s`

case ${hostname} in
  asa1) \
    instances=(
      #                     GPU     NET GPU OUT
      # mask  bind_host     DEV XID CPU CPU CPU
      '0x0606 192.168.2.41   0   1   1   8   8' # Instance 0, eth2
      '0x0606 192.168.2.42   0   3   2   8   8' # Instance 1, eth3
      '0x6060 192.168.2.141  1   5   5   4   4' # Instance 2, eth4
      '0x6060 192.168.2.142  1   0   6   4   4' # Instance 3, eth5
    );;

  paper5) \
    instances=( 
      #                     GPU     NET GPU OUT
      # mask  bind_host     DEV XID CPU CPU CPU
      '0x0606 192.168.2.5    0   2   2   3   3' # Instance 0, eth2
    );;
esac

function init() {
  instance=$1
  mask=$2
  bindhost=$3
  gpudev=$4
  xid=$5
  netcpu=$6
  gpucpu=$7
  outcpu=$8

  echo taskset $mask \
  ./paper_xgpu -I $instance \
    -o BINDHOST=$bindhost \
    -o GPUDEV=$gpudev \
    -o XID=$xid \
    -c $netcpu paper_net_thread \
    -c $gpucpu paper_gpu_thread \
    -c $outcpu paper_gpu_output_thread

  taskset $mask \
  ./paper_xgpu -I $instance \
    -o BINDHOST=$bindhost \
    -o GPUDEV=$gpudev \
    -o XID=$xid \
    -c $netcpu paper_net_thread \
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
  else
    echo Instance $1 not defined for host $hostname
  fi
  shift
  if [ -n "$1" ]
  then
    sleep 10
  fi
done
