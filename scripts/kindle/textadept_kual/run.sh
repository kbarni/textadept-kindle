#!/bin/sh
if [ $1 = "GTK" ]; then
	lipc-set-prop -s com.lab126.winmgr orientationLock R
	/mnt/us/textadept12/textadept.sh textadept-gtk
	lipc-set-prop -s com.lab126.winmgr orientationLock U
elif [ $1 = "KTERM" ]; then
	/mnt/us/extensions/kterm/bin/kterm.sh -e "bash /mnt/us/textadept12/textadept.sh textadept-curses"
else
	#./setlayout fr
	/mnt/us/extensions/kterm/bin/kterm.sh -k 0 -o R -e "bash /mnt/us/textadept12/textadept.sh textadept-curses"
fi
