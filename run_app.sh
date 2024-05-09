#!/bin/bash

app_path="/root/morse/test_app"

cd ${app_path}
if [ "${app_path}" != "$PWD" ];
then
 echo "ERROR: Path of test_app not correct"
 exit 1
fi

# Checking for correct directories
find bin/ -type d -name "Release" >/dev/null 2>&1
RET=$?
if [ ${RET} == 0 ];
then
 cd bin/Release
else
 echo "Error: Cannot change directory"
 exit 1
fi

./test_app /dev/morse_dev

exit 0
