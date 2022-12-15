readonly BASEDIR=$(readlink -f $(dirname $0))/../../../../

SCRIPT_PATH=$(readlink -f $(dirname $0))/
APP=$SCRIPT_PATH/test_opt_api_cpp

function run_app(){
## dynamic link
    export LD_LIBRARY_PATH=$BASEDIR/lib/smdk_allocator/lib
## common
    SMALLOC_CONF=use_auto_arena_scaling:true
#   echo $SMALLOC_CONF
    export SMALLOC_CONF

    $APP
}

run_app
ret=$?

echo
if [ $ret == 0 ]; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi

exit 0
