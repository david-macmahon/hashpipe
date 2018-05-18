#!/bin/bash

name=`basename $0`

usage="
usage: $name PATTERN SMP_AFFINITY [...]
SMP_AFFINITY(s) may be decimal or hexadecimal with leading '0x'"

logger="/usr/bin/logger -s -t ${name}"

pattern=${1:?"$usage"}
shift

smps=( "${@}" )
nsmps=${#smps[@]}

# Used to check for /sys/class/net/${PATTERN}/device/msi_irqs/ directory
sanitized_pattern=$(echo "${pattern}" | tr -cd '[A-Za-z0-9]')
msi_irqs_dir="/sys/class/net/${sanitized_pattern}/device/msi_irqs"

retries=0

while [ $retries -lt 2000 ]
do
  irqnums=($(awk -F: "/$pattern/{print \$1}"  /proc/interrupts))

  if [ -z "${irqnums}" -a -d "${msi_irqs_dir}" ]
  then
    irqnums=($(ls "${msi_irqs_dir}"))
  fi

  # If interrupts were found or we're only querying status, break
  if [ -n "${irqnums}" -o -z "${smps}" ]
  then
    break
  fi

  # Otherwise sleep 1 ms and try again (up to 2000 ms)
  /bin/sleep 0.001

  retries=$((retries + 1))
done

if [ $retries -gt 0 -a -n "${irqnums}" ]
then
  $logger "found interrupts for '$pattern' after ${retries} milliseconds"
fi

if [ -z "${irqnums}" ]
then
  $logger "no interrupts found for '$pattern'"
  exit 1
fi

smpidx=0
for ((i=0;i<${#irqnums[@]};i=i+1))
do
  irqnum=${irqnums[i]}
  irqnam=$(awk "/^ *${irqnum}:/{print \$NF}" /proc/interrupts)
  file="/proc/irq/$irqnum/smp_affinity"
  if [ ${nsmps} -gt 0 ]
  then
    printf %08x "${smps[$smpidx]}" > "$file" && \
    $logger "Set irq smp_affinity for "$irqnam" irq $irqnum to $(cat "$file")"
    smpidx=$(( (smpidx + 1) % nsmps ))
  else
    $logger "Current irq smp_affinity for "$irqnam" irq $irqnum is $(cat "$file" 2>/dev/null || echo 'unreadable')"
  fi
done

exit 0
