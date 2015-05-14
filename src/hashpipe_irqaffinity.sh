#!/bin/bash

name=`basename $0`

usage="
usage: $name PATTERN SMP_AFFINITY [...]
SMP_AFFINITY(s) may be decimal or hexadecimal with leading '0x'"

logger="logger -s -t $name"

pattern=${1:?"$usage"}
shift

smps=( "${@}" )
nsmps=${#smps[@]}

irqnums=($(awk -F: "/$pattern/{print \$1}"  /proc/interrupts))
irqnams=($(awk     "/$pattern/{print \$NF}" /proc/interrupts))

if [ -z "${irqnums}" ]
then
  $logger "no interrupts found for '$pattern'"
  exit 1
fi

smpidx=0
for ((i=0;i<${#irqnums[@]};i=i+1))
do
  irqnum=${irqnums[i]}
  irqnam=${irqnams[i]}
  file="/proc/irq/$irqnum/smp_affinity"
  if [ ${nsmps} -gt 0 ]
  then
    printf %08x "${smps[$smpidx]}" > "$file" && \
    $logger "Set irq smp_affinity for "$irqnam" irq $irqnum to $(cat "$file")"
    smpidx=$(( (smpidx + 1) % nsmps ))
  else
    $logger "Current irq smp_affinity for "$irqnam" irq $irqnum is $(cat "$file")"
  fi
done

exit 0
