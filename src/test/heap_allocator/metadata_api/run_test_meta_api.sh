#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../../

SCRIPT_PATH=$(readlink -f $(dirname $0))/

function run_app(){
## dynamic link
    export LD_LIBRARY_PATH=$BASEDIR/lib/smdk_allocator/lib/
## common
    SMALLOC_CONF=use_auto_arena_scaling:true
    export SMALLOC_CONF
    
    case "$APP" in
        api)
	    if [ -z $RUN_ON_QEMU ]; then
		$SCRIPT_PATH/test_metadata_api
	    else
		$SCRIPT_PATH/test_metadata_api size 20
	    fi
            ;;
        node)
	    if [ -z $RUN_ON_QEMU ]; then
		$SCRIPT_PATH/test_metadata_api_node
	    else
		$SCRIPT_PATH/test_metadata_api_node size 20
	    fi
            ;;
        *)
            ;;
    esac
}


echo [TEST_METADATA_API]

APP=api
run_app
ret=$?

echo
if [ $ret == 0 ]; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi


echo
echo [TEST_METADATA_API_NODE]

APP=node
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

