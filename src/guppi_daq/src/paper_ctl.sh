#!/bin/bash

function start() {
  sync=$1
  count=${2:-10}
  if [ -n "${sync}" ]
  then
    ./check_guppi_status -k INTSYNC  -s $sync
    ./check_guppi_status -k INTCOUNT -s $count
    ./check_guppi_status -k INTSTAT  -s start
  else
    help
    exit 1
  fi
}

function stop() {
  ./check_guppi_status -k INTSTAT  -s stop
}

function help() {
  echo "Invalid syntax (TODO: make this help message more useful)"
}

case $1 in
  start) shift; start "$@";;
  stop) stop;;
  *) help; exit 1;;
esac
