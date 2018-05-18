#!/bin/bash

do_all=1
do_cpu=0
do_net=0
do_gpu=0
sep=-n

shopt -s nullglob

while getopts ":ceghn" opt; do
  case $opt in
    c) do_cpu=1; do_all=0;;
    e) do_net=1; do_all=0;;
    n) do_net=1; do_all=0;;
    g) do_gpu=1; do_all=0;;
    h) echo "Usage: `basename $0` [-c] [-e|-n] [-g]"
       echo "  -c       for CPU topology"
       echo "  -e or -n for NET topology"
       echo "  -g       for GPU topology"
       echo "Default is all of the above."
       exit
       ;;
    \?)
      echo "Invalid option: -$OPTARG" >&2
      exit
      ;;
  esac
done

if [ $do_cpu == 1 -o $do_all == 1 ]
then
  # Note: More than one CPU per socket/core thanks to hyperthreading
  echo $sep
  echo Sockets/cores to CPUs:
  egrep 'processor|core.id|physical.id' /proc/cpuinfo \
    | paste - - - \
    | sort -t: -k3,3n -k4,4n -k2,2n \
    | awk '{printf "socket %d, core %2d -> cpu %2d\n", $7, $11, $3}'
  sep=''
fi

if [ $do_net == 1 -o $do_all == 1 ]
then
  echo $sep
  echo Ethernet interfaces to CPUs:
  for i in /sys/class/net/eth*
  do
    echo -n "$(basename $i): "
    cat $i/device/local_cpulist
  done
  sep=''
fi

if [ $do_gpu == 1 -o $do_all == 1 ] && [ -d /proc/driver/nvidia ]
then
  echo $sep
  echo GPUs to CPUs:
  for d in /proc/driver/nvidia/gpus/*
  do
    gpuid=`awk '/Device Minor/{gsub(/[.:]/,"?",$3); print $3}' $d/information`
    echo -n "gpu${gpuid:-$(basename $d)}: "
    cat /sys/bus/pci/devices/`awk '/Bus Location/{gsub(/[.:]/,"?",$3); print $3}' $d/information`/local_cpulist
  done
  sep=''
fi
#~gpu0: 0-3,8-11
#~gpu1: 4-7,12-15
