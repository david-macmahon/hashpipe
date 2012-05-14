#!/bin/bash

PATH="$(dirname $0):${PATH}"

function get_sync_mcnt() {
  echo $(( $(check_guppi_status -Q GPUMCNT 2>/dev/null) + 1024 ))
}

function start() {
  count=${1-2048}
  sync=${2:-$(get_sync_mcnt)}
  if [ -n "${sync}" ]
  then
    check_guppi_status -k INTSYNC  -s $sync
    check_guppi_status -k INTCOUNT -s $count
    check_guppi_status -k INTSTAT  -s start
  else
    help
    exit 1
  fi
}

function stop() {
  check_guppi_status -k INTSTAT  -s stop
}

function help() {
  echo "$(basename $0) start [count [start_mcnt]]"
  echo "$(basename $0) stop"
}

case $1 in
  start) shift; start "$@";;
  stop) shift; stop "$@";;
  test) shift; get_sync_mcnt;;
  *) help; exit 1;;
esac
