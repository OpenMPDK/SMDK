readonly BASEDIR=$(readlink -f $(dirname $0))/../../../../

function run_app(){
## dynamic link
	export LD_LIBRARY_PATH=$BASEDIR/lib/smdk_allocator/lib
## common
	SMALLOC_CONF=use_auto_arena_scaling:true
#	echo $SMALLOC_CONF
	export SMALLOC_CONF

	./test_opt_api_cpp
}

run_app
