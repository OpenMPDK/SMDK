#!/usr/bin/env bash
# prerequisite: redis installation
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
#set -x
REDIS=redis-6.2.1
REDISDIR=$BASEDIR/lib/$REDIS/src/

# Redis server IP list
REDIS_SERVER_1=localhost
REDIS_SERVER_2=
REDIS_SERVER_3=

# Redis server port list
REDIS_PORT_1=6379
REDIS_PORT_2=
REDIS_PORT_3=

# Benchmark parameters
CLIENTS=1
REQUESTS=10000
#SIZE=524288
SIZE=262144
KEYSPACELEN=$REQUESTS

unset LD_PRELOAD
CXLMALLOC=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so
export LD_PRELOAD=$CXLMALLOC

cd $REDISDIR

while [ $# -gt 1 ]
do
	key="$1"
	case $key in
		--value_size)
			SIZE="$2"
			shift
			;;
		--conn_num)
			CLIENTS="$2"
			shift
			;;
		--call_num)
			REQUESTS="$2"
			shift
			;;
		*)
			echo "Unknown key: $key"
			echo "Keys:"
			echo "--value_size (Value size in Byte)"
			echo "--conn_num (Number of connections)"
			echo "--call_num (Number of calls per connection)"
			exit 1
			;;
	esac
	shift
done

REQUESTS=$[$CLIENTS*$REQUESTS]
KEYSPACELEN=$REQUESTS

echo "Data size: $SIZE"
echo "Number of clients: $CLIENTS"
echo "Number of requests: $REQUESTS"

echo "SET:"
if [ "$REDIS_SERVER_1" != "" ]; then
	$REDISDIR/redis-benchmark -h $REDIS_SERVER_1 -p $REDIS_PORT_1 -c $CLIENTS -n $REQUESTS -d $SIZE -r $KEYSPACELEN -t set | grep throughput &
fi
if [ "$REDIS_SERVER_2" != "" ]; then
	$REDISDIR/redis-benchmark -h $REDIS_SERVER_2 -p $REDIS_PORT_2 -c $CLIENTS -n $REQUESTS -d $SIZE -r $KEYSPACELEN -t set | grep throughput &
fi
if [ "$REDIS_SERVER_3" != "" ]; then
	$REDISDIR/redis-benchmark -h $REDIS_SERVER_3 -p $REDIS_PORT_3 -c $CLIENTS -n $REQUESTS -d $SIZE -r $KEYSPACELEN -t set | grep throughput &
fi

wait

echo "GET:"
if [ "$REDIS_SERVER_1" != "" ]; then
	$REDISDIR/redis-benchmark -h $REDIS_SERVER_1 -p $REDIS_PORT_1 -c $CLIENTS -n $REQUESTS -d $SIZE -r $KEYSPACELEN -t get | grep throughput &
fi
if [ "$REDIS_SERVER_2" != "" ]; then
	$REDISDIR/redis-benchmark -h $REDIS_SERVER_2 -p $REDIS_PORT_2 -c $CLIENTS -n $REQUESTS -d $SIZE -r $KEYSPACELEN -t get | grep throughput &
fi
if [ "$REDIS_SERVER_3" != "" ]; then
	$REDISDIR/redis-benchmark -h $REDIS_SERVER_3 -p $REDIS_PORT_3 -c $CLIENTS -n $REQUESTS -d $SIZE -r $KEYSPACELEN -t get | grep throughput &
fi

wait

#set,lpush,incr,hset
