#!/usr/bin/env bash
###################################################################
# Prerequisite
#  1) SMDK kernel is running.
#  2) SMDK allcator, bwd are built.
#  3) bwd.conf is modified accordingly.
###################################################################

readonly BASEDIR=$(readlink -f $(dirname $0))/../../
source "$BASEDIR/script/common.sh"

SCRIPT_PATH=$(readlink -f $(dirname $0))
RESULT_PATH=$SCRIPT_PATH/TC_result
TC_PATH=$BASEDIR/src/test/
PASS_COUNT=0
FAIL_COUNT=0

testcase=()
testcase+=("bwd/run_bwd_allocator_test.sh")
testcase+=("bwd/run_bwd_daemon_test.sh")
testcase+=("bwd/run_bwd_driver_test.sh")
testcase+=("bwd/run_bwd_plugin_test.sh")

function print_pass_fail(){
    SCRIPT_NAME=$(basename $1)
    $* >>$RESULT_PATH/$SCRIPT_NAME.log 2>&1
    ret=$?

    echo "" >>$RESULT_PATH/$SCRIPT_NAME.log

    if [ $ret != 0 ]; then
        echo ${red}"FAILED!($ret)"${rst}
        echo "Check TC output & error log: $RESULT_PATH/$SCRIPT_NAME.log"
        ((FAIL_COUNT++))
    else
        echo ${green}"PASSED!"${rst}
        ((PASS_COUNT++))
    fi

    return $ret
}

function run_test(){
    LINE="$*"
    printf  "  %-3s   %-70s : " ${green}"RUN"${rst} "${LINE}"
    print_pass_fail $*
    return $?
}

if [ `whoami` != 'root' ]; then
    log_error "Test requires root privileges"
    exit 2
fi

rm -rvf $RESULT_PATH
mkdir -p $RESULT_PATH

tc=1
for i in "${testcase[@]}"
do
    echo
    echo "TC ${tc} starts..."
    ((tc++))

    script=$(echo $i | cut -d ' ' -f1)

    if [ ! -f $TC_PATH/$script ]; then
        echo "$TC_PATH/$script: script not found"
        continue
    fi

    run_test $(realpath $TC_PATH/$i)

done

printf "\n\n"
printf "Total ${#testcase[@]} TCs executed: "
printf " %-2s %-6s," "${PASS_COUNT}" ${green}"PASSED"${rst}
printf " %-2s %-6s\n" "${FAIL_COUNT}" ${red}"FAILED"${rst}

echo
