#!/bin/bash

###############################################################################
# (C)2009 Red Hat, Inc., Lukas Czerner <lczerner@redhat.com>

BINARY=$(basename $0 .sh)
DIRNAME=$(dirname "$(pwd)/$0")
DATAFILE=${DIRNAME}/output.dat
PLOTSCRIPT=${DIRNAME}/plot.dis
GNUPLOT=$(which gnuplot)
PS2PDF=$(which ps2pdf)
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

$DIRNAME/$BINARY $@ | tee $DATAFILE &
pid=$!

wait $pid
retval=$?

if [ $retval == 0 ]; then

	echo "Returning success retval... (retval=$retval)"
	if [ ! -f $GNUPLOT ]; then
		echo "Gnuplot not found. Graphs will not be generated."
		exit 0
	fi

	echo "Generating graphs..."
	$GNUPLOT $PLOTSCRIPT
	if [ ! -f $PS2PDF ]; then
		echo "ps2pdf not found. Pdf file will not be generated,"
		echo "Graphs are saved in the file graphs.ps"
		exit 0
	fi

	$PS2PDF graphs.ps graphs.pdf
	echo "Graphs are saved in the file graphs.pdf"
	exit 0
else
	echo "Returning error retval... (retval=$retval)"
	exit 1
fi
