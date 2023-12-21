#!/usr/bin/env bash
readonly BASEDIR=$(readlink -f $(dirname $0))/../../../../

PRIORITY=normal

JAVA=java
JAVAC=javac

# option to resolve java.lang.OOM:java heap space
JAVA_HEAP="-Xmx5g"

function run_java(){
    cd $BASEDIR/src/test/heap_allocator/comp_api_java/javaTest
    #location of junit4.jar / junit.jar need modification for each system
    $JAVAC -cp /usr/share/java/junit4.jar javaHeapTest.java javaHeapUtil.java
    unset LD_PRELOAD
    CXLMALLOC=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so
    export LD_PRELOAD=$CXLMALLOC
    CXLMALLOC_CONF=use_exmem:true,exmem_size:65536,normal_size:65536,maxmemory_policy:remain
    if [ "$PRIORITY" == 'exmem' ]; then
        CXLMALLOC_CONF+=,priority:exmem
    elif [ "$PRIORITY" == 'normal' ]; then
        CXLMALLOC_CONF+=,priority:normal
    fi

    export CXLMALLOC_CONF
    echo $CXLMALLOC_CONF

    #path of junit4.jar / junit.jar need modification for each system
    $JAVA $JAVA_HEAP -cp /usr/share/java/junit4.jar:. org.junit.runner.JUnitCore javaHeapTest
}

function build_jnilib(){
    rm -rf $BASEDIR/src/test/heap_allocator/comp_api_java/jniTest/build
    mkdir $BASEDIR/src/test/heap_allocator/comp_api_java/jniTest/build
    cd $BASEDIR/src/test/heap_allocator/comp_api_java/jniTest/build
    cmake ../
    make
    ls
    cd ..
    echo "Build finished"
}

function run_jni(){
    build_jnilib
    $JAVAC javaJNITest.java

    unset LD_PRELOAD
    CXLMALLOC=$BASEDIR/lib/smdk_allocator/lib/libcxlmalloc.so
    export LD_PRELOAD=$CXLMALLOC
    CXLMALLOC_CONF=use_exmem:true,exmem_size:65536,normal_size:65536,maxmemory_policy:remain
    if [ "$PRIORITY" == 'exmem' ]; then
        CXLMALLOC_CONF+=,priority:exmem
    elif [ "$PRIORITY" == 'normal' ]; then
        CXLMALLOC_CONF+=,priority:normal
    fi

    export CXLMALLOC_CONF
    echo $CXLMALLOC_CONF

    $JAVA javaJNITest $PRIORITY
}

function usage(){
    echo "Usage: $0 [-e | -n] [-a | -j]"
    exit 2
}

MEM_SET=0
APP=0

while getopts ":enaj" opt; do
    case "$opt" in
        e)
            if [ $MEM_SET == 0 ]; then
                PRIORITY='exmem'
                MEM_SET=1
            fi
            ;;
        n)
            if [ $MEM_SET == 0 ]; then
                PRIORITY='normal'
                MEM_SET=1
            fi
            ;;
        a)
            if [ $APP == 0 ]; then
                APP="java"
            fi
            ;;
        j)
            if [ $APP == 0 ]; then
                APP="jni"
            fi
            ;;
        :)
            usage
            ;;
    esac
done

case "$APP" in
    java)
        run_java
        ret=$?
        ;;
    jni)
        run_jni
        ret=$?
        ;;
    *)
        usage
        ;;
esac

echo
if [ $ret == 0 ]; then
    echo "PASS"
else
    echo "FAIL"
    exit 1
fi

exit 0

