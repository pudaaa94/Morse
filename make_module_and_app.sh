#!/bin/bash
echo "###### BUILDING DRIVER #######"

export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-

make clean
RET=$?
if [ ${RET} -eq 0 ];
then
 echo "### CLEAN of Driver enviorment successful ###"
else
 echo "### CLEAN of Driver enviorment unsuccessful ###"
 exit 1
fi


make
if [ ${RET} -eq 0 ];
then
 echo "### MAKE of Driver Successful ###"
else
 echo "### MAKE of Driver Unsuccessful ###"
 exit 1
fi


echo "###### BUILDING TEST_APP ######"
app_path="/home/student/linux-kernel-labs/modules/nfsroot/root/morse/test_app"

cd ${app_path}
if [ "${app_path}" != "$PWD" ];
then
 echo "ERROR: Path of test_app not correct"
 exit 1
fi

make clean
RET=$?
if [ ${RET} -eq 0 ];
then
 echo "### CLEAN of test_app enviorment successful ###"
else
 echo "### CLEAN of test_app enviorment unsuccessful ###"
 exit 1
fi


make
if [ ${RET} -eq 0 ];
then
 echo "### MAKE of test_app successful ###"
else
 echo "### MAKE of test_app unsuccessful ###"
 exit 1
fi

echo "### BUILD OF DRIVER AND APP FINISHED SUCCESSFUL"
exit 0
