#!/usr/bin/env bash
# prerequisite: /path/to/SMDK/script/dep_pkg_install.sh

readonly BASEDIR=$(readlink -f $(dirname $0))/..
cd $BASEDIR/lib
#set -x
set -e

source "$BASEDIR/script/common.sh"
LIBSMDK=smdk_allocator
SMDK_PY=smdk_allocator/opt_api/py_smdk_pkg
SMDK_PYPACKAGE=_py_smdk.so
JEMALLOC=jemalloc-5.2.1
REDIS=redis-6.2.1
MEMCACHED=memcached-1.6.9
MEMTIER=memtier_benchmark-1.3.0
STREAM=stream
PCM=pcm
UPROF=AMDuProf_Linux_x64_4.0.341
NUMACTL=numactl-2.0.16
CXL_KERNEL=linux-6.9-smdk
CXL_KERNEL_CONFIG=config-linux-6.9-smdk
QEMU=qemu-8.1.50
MLC=mlc
VOLTDB=$BASEDIR/src/app/voltdb
CXLCLI=cxl_cli
TIERD=tierd
LIBPNM=PNMLibrary-pnm-v3.0.0

SMDK_BIN=$BASEDIR/lib/SMDK_bin
SMDK_VERSION=v2.1

function build_voltdb() {
	app=$VOLTDB
	log_normal "[build $app]"

	cd $app/voltdb_src
	if [ ! -f "/bundles/kafkastream10.jar" ]; then
		ant
	fi
	ret=$?
	cd -

	source $app/voltdb_src/bin/voltenv
	cd $app/voltdb_src/examples/voltkv
	javac -classpath $CLIENTCLASSPATH client/voltkv/*.java
	jar cf voltkv-client.jar -C client voltkv
	rm -rf client/voltkv/*.class
	cd -

	if [ $ret = 0 ]; then
		log_normal "[build $app]..success"
	else
		log_error "[build $app]..error"
	fi
}


function build_numactl() {
	app=$NUMACTL
	log_normal "[build $app]"

	cd $app
	if [ ! -f "numactl" ]; then
		autoreconf -f -i
		./configure && make -j
	fi
	ret=$?
	cd -

	if [ $ret = 0 ]; then
		log_normal "[build $app]..success"
	else
		log_error "[build $app]..error"
	fi

}

function build_qemu(){
	app=$QEMU
	log_normal "[build $app]"

	mkdir -p qemu/$app/build
	cd qemu/$app/build
	if [ ! -f "qemu-system-x86_64" ]; then
		../configure --target-list=x86_64-softmmu --enable-debug --enable-slirp
		make -j
	fi
	ret=$?
	cd -

	if [ $ret = 0 ]; then
		log_normal "[build $app]..success"
	else
		log_error "[build $app]..error"
	fi
}

function build_bm(){
	app=$STREAM
	log_normal "[build $app]"

	cd $app
	if [ ! -f "stream_c.exe" ]; then
		make -j
	fi
	ret=$?
	cd -

	if [ $ret = 0 ]; then
		log_normal "[build $app]..success"
	else
		log_error "[build $app]..error"
	fi
}

function build_redis(){
	app=$REDIS
	log_normal "[build $app]"
	if [ ! -d "$app" ]; then
		log_normal "extract $app"
		tar -xvf $app.tar.gz 1>/dev/null
		log_normal "extract $app..done"
	fi

	cd $app
	if [ ! -f "src/redis-server" ]; then
		make -j MALLOC=libc 
		make install PREFIX=$SMDK_BIN
	fi
	ret=$?
	cd -

	if [ $ret = 0 ]; then
		log_normal "[build $app]..success"
	else
		log_error "[build $app]..error"
	fi
}

function build_memcached(){
	app=$MEMCACHED
	log_normal "[build $app]"

	cd $app
	if [ ! -f "memcached" ]; then
		autoreconf -f -i
		./configure --prefix=$SMDK_BIN --enable-extstore && make -j && make install
	fi
	ret=$?
	cd -

	if [ $ret = 0 ]; then
		log_normal "[build $app]..success"
	else
		log_error "[build $app]..error"
	fi
}

function build_memtier(){
	app=$MEMTIER
	log_normal "[build $app]"
	if [ ! -d "$app" ]; then
                log_normal "extract $app"
                tar -xvf $app.tar.gz 1>/dev/null
                log_normal "extract $app..done"
	fi

	cd $app
	if [ ! -f "memtier_benchmark" ]; then
		autoreconf -ivf && ./configure --prefix=$SMDK_BIN && make -j
	fi
	ret=$?
	cd -

	if [ $ret = 0 ]; then
		log_normal "[build $app]..success"
	else
		log_error "[build $app]..error"
	fi
}

function build_smdk_kernel(){
	app=$CXL_KERNEL
	log_normal "[build $app]"

	cd $app
	if [ ! -f "arch/x86_64/boot/bzImage" ]; then
		cp ../$CXL_KERNEL_CONFIG .config
		#Note : make -j for building kernel would case a build error
		make oldconfig && make -j 4
	fi
	ret=$?
	cd -

	if [ $ret = 0 ]; then
		log_normal "[build $app]..success"
	else
		log_error "[build $app]..error"
	fi
}

function build_libsmdk(){
	app=$LIBSMDK
	log_normal "[build libsmdk]"

	if [ ! -e "$LIBPNM/PNMLibrary/build/libs/lib/libpnm.so" ]; then
		log_error "[build libsmdk] build PNMLibrary first(libpnm)..error"
		return
	fi

	if [ ! -f "$app/lib/libcxlmalloc.a" ] || [ ! -f "$app/lib/libsmalloc.a" ] ; then
		cd $app/$JEMALLOC
		autoconf
		./configure --with-jemalloc-prefix='je_' --with-private-namespace='' --disable-cxx
		cd - && cd $app
		make
		cd -
	fi
	ret=$?

	if [ $ret = 0 ]; then
		log_normal "[build libsmdk]..success"
	else
		log_error "[build libsmdk]..error"
	fi
}

function build_py_smdk(){
	smdklib=$LIBSMDK/lib
	app=$SMDK_PY
	log_normal "[build py_smdk]"

	if [ ! -e "$smdklib/libsmalloc.so" ]; then
		log_error "[build py_smdk] build SMDK library first(libsmdk)..error"
		return
	fi

	cd $app/py_smdk && python3 build.py && cd -
	mv $app/py_smdk/_py_smdk*so $app/$SMDK_PYPACKAGE
	ret=$?

	if [ $ret = 0 ]; then
		log_normal "[build py_smdk]..success"
	else
		log_error "[build py_smdk]..error"
	fi
}

function build_cxl_cli(){
	app=$CXLCLI
	log_normal "[build $app]"

	cd $app && meson setup build
	cd -

	cd $app/build && ninja
	ret=$?
	cd -

	if [ $ret = 0 ]; then
		log_normal "[build $app]..success"
	else
		log_error "[build $app]..error"
	fi
}

function build_tierd(){
	app=$PCM
	log_normal "[build $app]"

	mkdir -p $TIERD/$app/build
	cd $TIERD/$app/build
	cmake_version=$(cmake --version | awk 'NR==1{print $3}')
	if [[ $cmake_version < "3.12.0" ]]; then
		cmake .. && cmake --build .
	else
		cmake .. && cmake --build . -j
	fi
	ret=$?
	cd -

	if [ $ret = 0 ]; then
		log_normal "[build $app]..success"
	else
		log_error "[build $app]..error"
	fi

	app=$UPROF
	log_normal "[unzip $app]"

	ret=0
	if [ ! -f "$TIERD/$app/bin/AMDuProfPcm" ]; then
		cd $TIERD
		tar -xvf $app.tar.bz2 1>/dev/null
		ret=$?
		cd -
	fi

	if [ $ret = 0 ]; then
		log_normal "[unzip $app]..success"
	else
		log_error "[unzip $app]..error"
	fi

	for app in $TIERD; do
		log_normal "[build $app]"

		cd $app && make
		ret=$?
		cd -

		if [ $ret = 0 ]; then
			log_normal "[build $app]..success"
		else
			log_error "[build $app]..error"
		fi
	done
}

function build_libpnm(){
	app=$LIBPNM
	log_normal "[build $app]"

	if [ ! -d "$app" ]; then
		log_normal "extract $app"
		tar -xvf $app.tar.gz 1>/dev/null
		log_normal "extract $app..done"

		cd $app/PNMLibrary
		patch -p1 < ../patches/PNMLibrary/*.patch
		chmod +111 ./scripts/*.sh ./scripts/*.py
		cd -
	fi

	ret=0
	if [ ! -f "$app/PNMLibrary/build/libs/lib/libpnm.so" ]; then
		if [ ! -d "/usr/local/include/linux" ]; then
			sudo mkdir -p /usr/local/include/linux
		fi
		if [ ! -f "/usr/local/include/linux/imdb_resources.h" ]; then
			sudo cp $CXL_KERNEL/include/uapi/linux/imdb_resources.h /usr/local/include/linux/
		fi
		if [ ! -f "/usr/local/include/linux/sls_resources.h" ]; then
			sudo cp $CXL_KERNEL/include/uapi/linux/sls_resources.h /usr/local/include/linux/
		fi
		if [ ! -f "/usr/local/include/linux/pnm_resources.h" ]; then
			sudo cp $CXL_KERNEL/include/uapi/linux/pnm_resources.h /usr/local/include/linux/
		fi
		cd $app/PNMLibrary
		mkdir -p test_tables
		./scripts/build.sh -cb -j `nproc` -r --fsim
		ret=$?
		cd -
	fi

	if [ $ret = 0 ]; then
		log_normal "[build $app]..success"
	else
		log_error "[build $app]..error"
	fi
}

function build_all(){
	build_libpnm
	build_libsmdk
	build_redis
	build_memcached
	build_memtier
	build_bm
	build_numactl
	#build_voltdb
	#build_qemu
	#build_smdk_kernel
	build_py_smdk
	build_cxl_cli
	#build_tierd
}

function clean_libsmdk(){
	app=$LIBSMDK
	log_normal "[clean libsmdk]"
	if [ -d "$app" ]; then
		cd $app
		make clean
		cd -
	fi
	log_normal "[clean libsmdk]..done"
}

function clean_py_smdk(){
	app=$SMDK_PY
	log_normal "[clean py_smdk]"
	rm -rf $SMDK_PY/py_smdk/*_py*
	rm -rf $SMDK_PY/$SMDK_PYPACKAGE
	log_normal "[clean py_smdk]..done"
}

function clean_redis(){
	app=$REDIS
	log_normal "[clean $app]"
	if [ -d "$app" ]; then
		cd $app
		make clean && make distclean
		cd -
	fi
	log_normal "[clean $app]..done"
}

function clean_memcached(){
	app=$MEMCACHED
	log_normal "[clean $app]"
	if [ -d "$app" ]; then
		cd $app
		make clean && make distclean
		cd -
	fi
	log_normal "[clean $app]..done"
}

function clean_memtier(){
	app=$MEMTIER
	log_normal "[clean $app]"
	if [ -d "$app" ]; then
		cd $app
		make clean && make distclean
		cd -
	fi
	log_normal "[clean $app]..done"
}

function clean_bm(){
	app=$STREAM
	log_normal "[clean $app]"
	if [ -d "$app" ]; then
		cd $app
		make clean
		cd -
	fi
	log_normal "[clean $app]..done"
}

function clean_kernel(){
	app=$CXL_KERNEL
	log_normal "[clean $app]"
	if [ -d "$app" ]; then
		cd $app
		make clean && make distclean
		cd -
	fi
	log_normal "[clean $app]..done"
}

function clean_qemu(){
	app=$QEMU
	log_normal "[clean $app]"

	rm -rf qemu/$app/build
	ret=$?

	if [ $ret = 0 ]; then
		log_normal "[clean $app]..success"
	else
		log_error "[clean $app]..error"
	fi
}

function clean_numactl(){
	app=$NUMACTL
	log_normal "[clean $app]"

	cd $app
	if [ -f "numactl" ]; then
		make clean && make distclean
	fi
	ret=$?
	cd -

	if [ $ret = 0 ]; then
		log_normal "[clean $app]..success"
	else
		log_error "[clean $app]..error"
	fi
}

function clean_voltdb(){
	app=$VOLTDB
	log_normal "[clean $app]"

	cd $app/voltdb_src
	if [ -f "/bundles/kafkastream10.jar" ]; then
		ant clean
	fi
	ret=$?
	cd -

	if [ $ret = 0 ]; then
		log_normal "[clean $app]..success"
	else
		log_error "[clean $app]..error"
	fi
}

function clean_cxl_cli(){
	app=$CXLCLI
	log_normal "[clean $app]"

	cd $app && rm -rf build
	ret=$?
	cd -

	if [ $ret = 0 ]; then
		log_normal "[clean $app]..success"
	else
		log_error "[clean $app]..error"
	fi
}

function clean_tierd(){
	app=$PCM
	log_normal "[clean $app]"

	rm -rf $TIERD/$app/build
	ret=$?

	if [ $ret = 0 ]; then
		log_normal "[clean $app]..success"
	else
		log_error "[clean $app]..error"
	fi

	app=$UPROF
	log_normal "[clean $app]"

	rm -rf $TIERD/$app
	ret=$?

	if [ $ret = 0 ]; then
		log_normal "[clean $app]..success"
	else
		log_error "[clean $app]..error"
	fi

	for app in $TIERD; do
		log_normal "[clean $app]"

		cd $app && make clean
		ret=$?
		cd -

		if [ $ret = 0 ]; then
			log_normal "[clean $app]..success"
		else
			log_error "[clean $app]..error"
		fi
	done
}

function clean_libpnm(){
	app=$LIBPNM
	log_normal "[clean $app]"

	sudo rm -f /usr/local/include/linux/imdb_resources.h
	sudo rm -f /usr/local/include/linux/sls_resources.h
	sudo rm -f /usr/local/include/linux/pnm_resources.h
	rm -rf $app/PNMLibrary/build
	ret=$?

	if [ $ret = 0 ]; then
		log_normal "[clean $app]..success"
	else
		log_error "[clean $app]..error"
	fi
}

case "$1" in
	kernel)
		build_smdk_kernel
		;;

	libsmdk)
		build_libsmdk
		;;

	memcached)
		build_memcached
		;;

	memtier)
		build_memtier
		;;

	redis)
		build_redis
		;;

	bm)
		build_bm
		;;
		
	qemu)
		build_qemu
		;;

	voltdb)
		build_voltdb
		;;

	numactl)
		build_numactl
		;;

	py_smdk)
		build_py_smdk
		;;

	cxl_cli)
		build_cxl_cli
		;;

	tierd)
		build_tierd
		;;

	libpnm)
		build_libpnm
		;;

	all)
		build_all
		;;

	clean_kernel)
		clean_kernel
		;;

	clean_libsmdk)
		clean_libsmdk
		;;

	clean_memcached)
		clean_memcached
		;;

	clean_memtier)
		clean_memtier
		;;

	clean_redis)
		clean_redis
		;;

	clean_bm)
		clean_bm
		;;

	clean_qemu)
		clean_qemu
		;;

	clean_voltdb)
		clean_voltdb
		;;

	clean_numactl)
		clean_numactl
		;;

	clean_py_smdk)
		clean_py_smdk
		;;

	clean_cxl_cli)
		clean_cxl_cli
		;;

	clean_tierd)
		clean_tierd
		;;

	clean_libpnm)
		clean_libpnm
		;;

	clean_all)
		clean_libpnm
		clean_libsmdk
		clean_redis
		clean_memcached
		clean_memtier
		clean_bm
		#clean_kernel
		#clean_qemu
		clean_numactl
		#clean_voltdb
		clean_py_smdk
		clean_cxl_cli
		#clean_tierd
		;;
	*)
		echo "Usage: build_lib.sh {all|kernel|libsmdk|redis|memcached|memtier|qemu|numactl|voltdb|bm|py_smdk|cxl_cli|tierd|libpnm}"
		echo "Usage: build_lib.sh {clean_all|clean_kernel|clean_libsmdk|clean_redis|clean_memcached|clean_memtier|clean_qemu|clean_numactl|clean_voltdb|clean_bm|clean_py_smdk|clean_cxl_cli|clean_tierd|clean_libpnm}"
		exit 1
		;;
esac
