################################################################
#!/bin/bash
# common libraries 
################################################################
green=$(tput bold)$(tput setaf 2)
red=$(tput bold)$(tput setaf 1)
rst=$(tput sgr0)

function check_attr_set() { # attribute, like "e"
  case "$-" in
    *"$1"*) return 1 ;;
    *)    return 0 ;;
  esac
}

function print_pass_fail(){
    #$* > /dev/null 2>&1 # quite mode
    $* 2>&1				# verbose
    ret=$?
    if (( $ret )); then
        echo ${red}"FAILED!($ret)"${rst}
        echo "Failed running command: "
        echo  "   $*"
        #exit 1  # exit when a test fails
    else
        echo ${green}"PASSED!"${rst}
    fi
	return $ret
}

function run_test(){
    LINE="$*"
    printf  "  %-3s   %-70s : " ${green}"RUN"${rst} "${LINE::69}"
    print_pass_fail $*
	return $?
}

function set_red(){
	echo -e "\033[31m"
}
function set_green(){
	echo -e "\033[32m"
}

function set_white(){
	echo -e "\033[0m"
}

function log_normal(){
	set_green && echo $1 && set_white
}

function log_error(){
	set_red && echo $1 && set_white
}

function killprocess() {
    # $1 = process pid
    if [ -z "$1" ]; then
        exit 1
    fi

    if kill -0 $1; then
        if [ $(uname) = Linux ]; then
            process_name=$(ps --no-headers -o comm= $1)
        else
            process_name=$(ps -c -o command $1 | tail -1)
        fi
        if [ "$process_name" = "sudo" ]; then
            # kill the child process, which is the actual app
            # (assume $1 has just one child)
            local child
            child="$(pgrep -P $1)"
            echo "killing process with pid $child"
            kill $child
        else
            echo "killing process with pid $1"
            kill $1
        fi

        # wait for the process regardless if its the dummy sudo one
        # or the actual app - it should terminate anyway
        wait $1
    else
        # the process is not there anymore
        echo "Process with pid $1 is not found"
        exit 1
    fi
}

