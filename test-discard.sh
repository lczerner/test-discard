#!/bin/bash

###############################################################################
# (C)2009 Red Hat, Inc., Lukas Czerner <lczerner@redhat.com>

BINARY=$(basename $0 .sh)
DIRNAME=$(dirname "$(pwd)/$0")
DATAFILE=${DIRNAME}/output.dat
PLOTSCRIPT=${DIRNAME}/plot.dis
declare pid

do_exit() {
	kill -SIGUSR1 $pid
	wait $pid
	retval=$?
	if [ $retval -ge 0 ] && [ $retval -le 32 ]; then
		echo "Returning success retval... (retval=0)"
		exit 0
	else
		echo "Returning error retval... (retval=1)"
		exit 1
	fi
}

trap do_exit INT TERM

$DIRNAME/$BINARY $@ -t 50m -R 4k:1024k:4k -b | tee $DATAFILE &
pid=$!

wait $pid
retval=$?

if [ $retval == 0 ]; then

	echo "Returning success retval... (retval=$retval)"
	GNUPLOT=$(which gnuplot 2> /dev/null)
	if [ $? != 0 ]; then
		echo "Gnuplot not found. Graphs will not be generated."
		exit 0
	fi
	echo "Generating graphs..."
	$GNUPLOT $PLOTSCRIPT

	PS2PDF=$(which ps2pdf)
	if [ $? != 0 ]; then
		echo "ps2pdf not found. Pdf file will not be generated,"
		echo "Graphs are saved in the file ${DIRNAME}/graphs.ps"
		exit 0
	fi
	$PS2PDF graphs.ps graphs.pdf
	echo "Graphs are saved in the file ${DIRNAME}/graphs.pdf"

	exit 0
else
	echo "Returning error retval... (retval=$retval)"
	exit 1
fi
