#!/bin/bash

# xeng_init.sh - Initialize X engines

#xeng_hosts=$(echo px{1..8})
xeng_hosts=$(echo px{1..2})

paper_init=/home/davidm/local/src/paper_gpu/src/guppi_daq/src/paper_init.sh
hpr_gateway=/usr/local/bin/hashpipe_redis_gateway.rb

function start() {
  for x in $xeng_hosts
  do
    echo "Starting X engines on px${x}"
    ssh obs@$x "
      $paper_init 0 1;
      $hpr_gateway -g px${x} </dev/null >/dev/null 2>&1 &"
  done
}

function stop() {
  echo Not implemented yet.
}

case ${1} in
  start|stop) ${1};;
  *) echo "$(basename $0) {start|stop}"
esac
