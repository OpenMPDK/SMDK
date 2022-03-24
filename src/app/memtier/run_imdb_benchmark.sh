#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
readonly MEMTIER_BENCH=$BASEDIR/src/app/memtier/memtier_bench.sh
source "$BASEDIR/script/common.sh"
export PATH=$PATH:.

NTHREAD=1

##############################################################
# Number of clients
##############################################################
NCLIENT=4

##############################################################
# Number of connections between benchmark tool and IMDB server
##############################################################
NCONN=32 

##############################################################
# Number of calls per connection
##############################################################
NCALL=64

##############################################################
# Value size
##############################################################
NSIZE=4096

function run_memtier_bench(){
	if [ "$2" != "" ]; then
		NTHREAD=$2
	fi
	if [ "$3" != "" ]; then
		NCLIENT=$3
	fi
	if [ "$4" != "" ]; then
		NCALL=$4
	fi
	if [ "$5" != "" ]; then
		NSIZE=$5
	fi
	if [ "$6" != "" ]; then
		KEYMAX=$6
	fi
	if [ "$7" != "" ]; then
		KEYPATTERN=$7
	fi
	if [ "$8" != "" ]; then
		RATIO=$8
	fi

	if [ "$1" == "memcached" ]; then
		log_normal "do_memcached_test with memtier_benchmark"
		$MEMTIER_BENCH --app memcached --data_size $NSIZE --pipeline_num 1 --thread_num $NTHREAD \
			--client_num $NCLIENT --call_num $NCALL --key_max $KEYMAX --key_pattern $KEYPATTERN \
			--ratio $RATIO
	elif [ "$1" == "redis" ]; then
		$MEMTIER_BENCH --app redis --data_size $NSIZE --pipeline_num 1 --thread_num $NTHREAD \
			--client_num $NCLIENT --call_num $NCALL --key_max $KEYMAX --key_pattern $KEYPATTERN \
			--ratio $RATIO
	fi
}

##############################################################
# Usage:
#  run_memtier_bench NTHREAD NCLIENT NCALL NSIZE KEYMAX KEYPATTERN RATIO
##############################################################

##############################################################
# Memcached
##############################################################
#run_memtier_bench memcached 24 50 3495 4096 4194000 "P:P" "1:0"
#run_memtier_bench memcached 24 50 3495 4096 4194000 "G:G" "0:1"
#run_memtier_bench memcached 24 50 55 262144 66000 "P:P" "1:0"
#run_memtier_bench memcached 24 50 55 262144 66000 "G:G" "0:1"
#run_memtier_bench memcached 24 50 20 524288 32400 "P:P" "1:0"
#run_memtier_bench memcached 24 50 20 524288 32400 "G:G" "0:1"
#run_memtier_bench memcached 24 50 10 1048576 16800 "P:P" "1:0"
#run_memtier_bench memcached 24 50 10 1048576 16800 "G:G" "0:1"
#run_memtier_bench memcached 24 50 5 2097152 8400 "P:P" "1:0"
#run_memtier_bench memcached 24 50 5 2097152 8400 "G:G" "0:1"

##############################################################
# Redis
##############################################################
#run_memtier_bench redis 24 50 3495 4096 4194000 "P:P" "1:0"
#run_memtier_bench redis 24 50 34950 4096 4194000 "G:G" "0:1"
#run_memtier_bench redis 24 50 55 262144 66000 "P:P" "1:0"
#run_memtier_bench redis 24 50 546 262144 66000 "G:G" "0:1"
#run_memtier_bench redis 24 50 27 524288 32400 "P:P" "1:0"
#run_memtier_bench redis 24 50 273 524288 32400 "G:G" "0:1"
#run_memtier_bench redis 24 50 14 1048576 16800 "P:P" "1:0"
#run_memtier_bench redis 24 50 137 1048576 16800 "G:G" "0:1"
#run_memtier_bench redis 24 50 7 2097152 8400 "P:P" "1:0"
#run_memtier_bench redis 24 50 68 2097152 8400 "G:G" "0:1"
