#!/bin/bash


echo "#### REMOVING THE MODULE ####"
lsmod | grep "morse_dev"
RET=$?
if [ ${RET} == 0 ];
then
 rmmod morse_dev
 echo "### Removing module succseful ###"
else
 echo "### NOTHING TO BE DONE, MODULE REMOVED ###"
fi


echo "#### INSERTING THE MODULE ####"

insmod morse_dev.ko

major=$(cat /proc/devices | sed -n '/morse_dev/{s/[^0-9]//g;p}')

mknod /dev/morse_dev c ${major} 0
RET=$?
if [ ${RET} == 0 ];
then
 echo "### INSERTING MODULE SUCCSEFUL ###"
 exit 0
else
 echo "### INSERTING MODULE UNSUCCSEFUL ###"
 exit 1
fi