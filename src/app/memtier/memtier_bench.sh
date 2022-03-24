#!/usr/bin/env bash
# prerequisite: memcached installation
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
MEMTIERDIR=$BASEDIR/lib/memtier_benchmark-1.3.0/
#!/bin/bash

unset LD_PRELOAD

#set -x

APP=
IP=127.0.0.1
PORT=

DATASIZE=4096
PIPELINE=1
THREADS=1
CLIENTS=1
REQUESTS=1
KEY_MAXIMUM=42949673
KEY_PATTERN="P:P"
RATIO="1:1"

function run_test() {
	$MEMTIERDIR/memtier_benchmark -P $APP -s $IP -p $PORT \
		--pipeline=$PIPELINE -c $CLIENTS -t $THREADS -d $DATASIZE \
		--key-maximum=$KEY_MAXIMUM --key-pattern=$KEY_PATTERN \
		--ratio=$RATIO --requests=$REQUESTS \
		--distinct-client-seed --randomize --run-count=1 --hide-histogram
}

while [ $# -gt 1 ]
do
	key="$1"
	case $key in
		--app)
			if [ $2 == "redis" ]; then
				APP=redis
			elif [ $2 == "memcached" ]; then
				APP=memcache_text
			else
				echo "Wrong app: $2"
				exit 1
			fi
			shift
			;;
		--data_size)
			DATASIZE="$2"
			shift
			;;
		--pipeline_num)
			PIPELINE="$2"
			shift
			;;
		--thread_num)
			THREADS="$2"
			shift
			;;
		--client_num)
			CLIENTS="$2"
			shift
			;;
		--call_num)
			REQUESTS="$2"
			shift
			;;
		--key_max)
			KEY_MAXIMUM="$2"
			shift
			;;
		--key_pattern)
			KEY_PATTERN="$2"
			shift
			;;
		--ratio)
			RATIO="$2"
			shift
			;;
		*)
			echo "Unknown key: $key"
			echo "Keys:"
			echo "--app"
			echo "--data_size (default 4096)"
			echo "--pipeline_num (default 1)"
			echo "--thread_num (default 1)"
			echo "--client_num (default 1)"
			echo "--call_num (default 1)"
			echo "--key_max (default 42949673)"
			echo "--key_pattern (default P:P)"
			echo "--ratio (e.g. 1:1, 0:1, 1:0. default 1:1)"
			exit 1
			;;
	esac
	shift
done

if [ "$APP" == "" ]; then
	echo "error: Select APP!"
	exit 1
elif [ "$APP" == "redis" ]; then
	PORT=6379
elif [ "$APP" == "memcache_text" ] || [ $APP == "memcache_binary" ]; then
	PORT=11211
fi

echo "Start SET/GET test case"
run_test
