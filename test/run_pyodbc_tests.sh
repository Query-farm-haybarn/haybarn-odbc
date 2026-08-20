#!/bin/bash

set -e

#echo -e "[ODBC]\nTrace = yes\nTraceFile = /tmp/odbctrace\n\n[Haybarn Driver]\nDriver = "$(pwd)"/build/debug/libhaybarn_odbc.so" > ~/.odbcinst.ini
#echo -e "[Haybarn]\nDriver = Haybarn Driver\nDatabase=:memory:\n" > ~/.odbc.ini

BASE_DIR=$(dirname $0)

#Configuring ODBC files
$BASE_DIR/../linux_setup/unixodbc_setup.sh -u -D $(pwd)/build/debug/libhaybarn_odbc.so

export ASAN_OPTIONS=verify_asan_link_order=0

python3 test/pyodbc-test.py
if [[ $? != 0 ]]; then
    exit 1;
fi
