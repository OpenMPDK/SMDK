#!/usr/bin/env bash
# prerequisite: memcached installation
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../
MEMCACHED=memcached-1.6.9

EXTFILE=/opt/memcache_file
EXTSIZE=20g
OPTEXT="-o ext_page_size=2,ext_wbuf_size=2,ext_path=$EXTFILE:$EXTSIZE,ext_item_age=1,ext_threads=1,ext_compact_under=1000,ext_drop_under=1000,ext_direct_io=1"
#OPTEXT="-o ext_page_size=8,ext_wbuf_size=2,ext_path=/memcache_file:64m,ext_threads=1,ext_io_depth=2,ext_item_size=512,ext_item_age=2,ext_recache_rate=10000,ext_max_frag=0.9,slab_automove=0,ext_compact_under=1"
#OPTEXT="-o ext_path=/memcache_file:256m,ext_item_age=2"

PRIORITY=exmem

function run_app(){
        rm $EXTFILE* 2>/dev/null
        echo 3 > /proc/sys/vm/drop_caches

        CXLMALLOC_LIB=/usr/lib/libcxlmalloc.so
        CXLMALLOC_CONF=use_exmem:true,exmem_zone_size:4096,normal_zone_size:4096,maxmemory_policy:remain
        if [ "$PRIORITY" == 'exmem' ]; then
                CXLMALLOC_CONF+=,priority:exmem
        elif [ "$PRIORITY" == 'normal' ]; then
                CXLMALLOC_CONF+=,priority:normal
        fi
        echo $CXLMALLOC_CONF
        IMAGE_ID=memcached:smdk
        docker run -it -p 11211:11211 -e LD_PRELOAD=$CXLMALLOC_LIB -e CXLMALLOC_CONF=$CXLMALLOC_CONF $IMAGE_ID -v -m 65535 -c 1024 -u mem
}

while getopts "en" opt; do
        case "$opt" in
                e)
                        PRIORITY='exmem'
                        run_app
                        ;;
                n)
                        PRIORITY='normal'
                        run_app
                        ;;
                :)
                        echo "Usage: $0 -e[exmem] or $0 -n[normal]"
                        exit 1
                        ;;
        esac
done
