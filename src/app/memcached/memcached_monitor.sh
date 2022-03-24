#!/usr/bin/env bash
MEMCACHED_IP=localhost
MEMCACHED_PORT=11211
#set -x
unset LD_PRELOAD
telnet $MEMCACHED_IP $MEMCACHED_PORT

# stats
# stats slabs
# set key 0 100 10
# get key
# replace key 0 100 5
# get key
# stats items
# flush_all
# get key
# stats reset
# quit
