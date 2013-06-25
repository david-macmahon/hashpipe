#!/bin/bash

# xeng_init.sh - Initialize X engines

xeng_hosts=$(echo px{1..8})
instances="0 1"

paper_init=/usr/local/bin/paper_init.sh
hpr_gateway=/usr/local/bin/hashpipe_redis_gateway.rb
cudarc=/usr/local/cuda/cuda.sh

function start() {
  for x in $xeng_hosts
  do
    echo "Starting X engines on ${x}"
    ssh obs@$x "
      source $cudarc;
      $paper_init ${instances} </dev/null >/dev/null 2>&1;
      $hpr_gateway -g ${x} </dev/null >/dev/null 2>&1 &
    "
  done
  sleep 1
  echo 'Clearing MISSEDPK counters'
  redis-cli -h redishost publish hashpipe:///set MISSEDPK=0 > /dev/null
}

function stop() {
  for x in $xeng_hosts
  do
    echo "${2:-Stopping} X engines on ${x}"
    ssh obs@$x "
      pkill $1 hashpipe
      pkill $1 -f hashpipe_redis_gateway.rb
    "
  done
}

case ${1} in
  start|stop) ${1};;
  kill) stop -9 Killing;;
  *) echo "$(basename $0) {start|stop|kill}"
esac
