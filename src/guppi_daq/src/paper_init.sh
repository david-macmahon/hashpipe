#!/bin/bash

hostname=`hostname -s`

function getip() {
  out=$(host $1) && echo $out | awk '{print $NF}'
}

myip=$(getip $(hostname))

# Determine which, if any, pxN alias maps to IP of current host.
# If a pxN match is found, mypx gets set to N (i.e. just the numeric part).
# If no match is found, mypx will be empty.
mypx=
for p in {1..8}
do
  ip=$(getip px${p}.roach.pvt)
  [ "${myip}" == "${ip}" ] || continue
  mypx=$p
done

# If no pxN alias maps to IP of current host, abort
if [ -z ${mypx} ]
then
  echo "$(hostname) is not aliased to a pxN name"
  exit 1
fi

# Calculate XIDs based on mypx
xid0=$(( 2*(mypx-1)    ))
xid1=$(( 2*(mypx-1) + 1))

case ${hostname} in

  asa*)
    instances=(
      #                               GPU       NET FLF GPU OUT
      # mask  bind_host               DEV  XID  CPU CPU CPU CPU
      "0x0707 ${hostname}-2.tenge.pvt  0  $xid0  2   8   1   8" # Instance 0, eth2
      "0x7070 ${hostname}-4.tenge.pvt  1  $xid1  6  12   5  12" # Instance 1, eth4
    );;

  simech1)
    instances=(
      #                               GPU       NET FLF GPU OUT
      # mask  bind_host               DEV  XID  CPU CPU CPU CPU
      "0x0707 ${hostname}-2.tenge.pvt  0  $xid0  2   8   1   8" # Instance 0, eth2
      "0x7070 ${hostname}-3.tenge.pvt  1  $xid1  6  12   5  12" # Instance 1, eth3
    );;

  paper5)
    instances=( 
      #                               GPU       NET FLF GPU OUT
      # mask  bind_host               DEV  XID  CPU CPU CPU CPU
      "0x0606 ${hostname}-2.tenge.pvt  0  $xid0  2   4   3   4" # Instance 0, eth2
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
