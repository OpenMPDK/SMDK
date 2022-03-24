#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../

PRIORITY=normal
JAVA=java

# list of cluster nodes separated by commas in host:[port] format
SERVERS="localhost"

# voltkv config
KEYSIZE=32
MINVALUESIZE=1024
MAXVALUESIZE=1024
POOLSIZE=100000
DURATION=120

$BASEDIR/src/app/voltdb/voltdb_src/bin/sqlcmd < $BASEDIR/src/app/voltdb/voltdb_src/examples/voltkv/ddl.sql

# call script to set up paths, including
# java classpaths and binary paths
source $BASEDIR/src/app/voltdb/voltdb_src/bin/voltenv

cd $BASEDIR/src/app/voltdb/voltdb_src/examples/voltkv
$JAVA -classpath voltkv-client.jar:$CLIENTCLASSPATH voltkv.AsyncBenchmark \
    --displayinterval=5 \
    --duration=$DURATION \
    --servers=$SERVERS \
    --poolsize=$POOLSIZE \
    --preload=true \
    --getputratio=0.90 \
    --keysize=$KEYSIZE \
    --minvaluesize=$MINVALUESIZE \
    --maxvaluesize=$MAXVALUESIZE \
    --entropy=127 \
    --usecompression=false
