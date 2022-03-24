readonly BASEDIR=$(readlink -f $(dirname $0))/../../../../

ARGS=""

function run_app(){
## dynamic link
    export LD_LIBRARY_PATH=$BASEDIR/lib/smdk_allocator/lib/
## common
    SMALLOC_CONF=use_auto_arena_scaling:true
    export SMALLOC_CONF

    ./test_metadata_api $ARGS
}

ARGS=$@
run_app
