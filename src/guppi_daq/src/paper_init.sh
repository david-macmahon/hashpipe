#!/bin/bash

hostname=`hostname -s`

# Hostname-to-XID map
declare -A xid

# Default XIDs (for development convenience)
xid[asa1-2]=0
xid[asa1-4]=1
xid[asa2-2]=0
xid[asa2-4]=1
xid[asa3-2]=0
xid[asa3-4]=1
xid[asa4-2]=0
xid[asa4-4]=1
xid[asa5-2]=0
xid[asa5-4]=1
xid[asa6-2]=0
xid[asa6-4]=1
xid[asa7-2]=0
xid[asa7-4]=1
xid[asa8-2]=0
xid[asa8-4]=1
xid[asa9-2]=0
xid[asa9-4]=1
xid[asa10-2]=0
xid[asa10-4]=1
xid[asa11-2]=0
xid[asa11-4]=1
xid[simech1-2]=0
xid[simech1-3]=1
xid[paper5-2]=10

# The "real" XIDs 0 to 15 - Update as needed to reflect local reality
# These are defined after the defaults to override them!
xid[asa1-2]=0
xid[asa1-4]=1
xid[asa2-2]=2
xid[asa2-4]=3
xid[asa3-2]=4
xid[asa3-4]=5
xid[asa4-2]=6
xid[asa4-4]=7
xid[asa5-2]=8
xid[asa5-4]=9
xid[asa6-2]=10
xid[asa6-4]=11
xid[asa7-2]=12
xid[asa7-4]=13
xid[asa8-2]=14
xid[asa8-4]=15

case ${hostname} in

  asa*)
    instances=(
      #                               GPU                       NET FLF GPU OUT
      # mask  bind_host               DEV   XID                 CPU CPU CPU CPU
      "0x0707 ${hostname}-2.tenge.pvt  0  ${xid[${hostname}-2]}  2   8   1   8" # Instance 0, eth2
      "0x7070 ${hostname}-4.tenge.pvt  1  ${xid[${hostname}-4]}  6  12   5  12" # Instance 1, eth4
    );;

  simech1)
    instances=(
      #                               GPU                       NET FLF GPU OUT
      # mask  bind_host               DEV   XID                 CPU CPU CPU CPU
      "0x0707 ${hostname}-2.tenge.pvt  0  ${xid[${hostname}-2]}  2   8   1   8" # Instance 0, eth2
      "0x7070 ${hostname}-3.tenge.pvt  1  ${xid[${hostname}-2]}  6  12   5  12" # Instance 1, eth3
    );;

  paper5)
    instances=( 
      #                               GPU                       NET FLF GPU OUT
      # mask  bind_host               DEV   XID                 CPU CPU CPU CPU
      "0x0606 ${hostname}-2.tenge.pvt  0  ${xid[${hostname}-2]}  2   4   3   4" # Instance 0, eth2
    );;

  *)
    echo "This host (${hostname}) is not supported by $(basename $0)"
    exit 1
    ;;
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

  if [ -z "${mask}" ]
  then
    echo "Invalid instance number '${instance}' (ignored)"
    return 1
  fi

  if [ -z "$outcpu" ]
  then
    echo "Invalid configuration for host ${hostname} instance ${instance} (ignored)"
    return 1
  fi

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

if [ -z "$1" ]
then
  echo "Usage: $(basename $0) INSTANCE_ID [...]"
  exit 1
fi

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
