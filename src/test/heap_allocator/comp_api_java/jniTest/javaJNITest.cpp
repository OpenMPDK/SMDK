//
// Created by alex on 2/22/17.
//


#include <iostream>
#include <complex>
#include <vector>
#include <algorithm>
#include "javaJNITest.h"

#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

/* Note: MAP_EXMEM must be identical with MAP_EXMEM at linux-5.17-rc5-smdk/include/uapi/asm-generic/mman-common.h */
#ifndef MAP_EXMEM
#define MAP_EXMEM 0x200000
#endif

/*
 * =================IMPLEMENTATION===============
 * Class:     javaJNITest
 * Method:    printMethod
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_javaJNITest_printMethod
        (JNIEnv *env, jobject obj) {
    std::cout << "Native method called. Printing garbage." << std::endl;
}

/*
 * =================IMPLEMENTATION===============
 * Class:     javaJNITest
 * Method:    trueFalse
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_javaJNITest_trueFalse
        (JNIEnv *env, jobject obj) {
    std::cout << "BOOL VALUE: 1 (True)" << std::endl;
    jboolean b = 1;
    return b;
}

/*
 * =================IMPLEMENTATION===============
 * Class:     javaJNITest
 * Method:    power
 * Signature: (II)I
 */
JNIEXPORT jint JNICALL Java_javaJNITest_power
        (JNIEnv *env, jobject obj, jint i1, jint i2) {
    int __i1_n = i1;
    int __i2_n = i2;
    return (jint) std::pow(__i1_n, __i2_n);
}

/*
 * ================IMPLEMENTATION================
 * Class:     javaJNITest
 * Method:    returnAByteArray
 * Signature: ()[B
 */
JNIEXPORT jbyteArray JNICALL Java_javaJNITest_returnAByteArray
        (JNIEnv *env, jobject obj) {
    jbyteArray __ba = env->NewByteArray(3);
    std::vector<unsigned char> __c_vec(3);
    __c_vec[0] = 0;
    __c_vec[1] = 1;
    __c_vec[2] = 1;
    unsigned char * __c_ptr = __c_vec.data();
    env->SetByteArrayRegion (__ba, 0, 3, reinterpret_cast<jbyte*>(__c_ptr));
    std::cout << "Printing Byte Array members..." << std::endl;
    std::for_each(__c_vec.begin(), __c_vec.end(), [](const char &c) { std::cout << c ; });
    std::cout << std::endl << std::endl;
    return __ba;
}

/*
 * ==============IMPLEMENTATION=================
 * Class:     javaJNITest
 * Method:    stringManipulator
 * Signature: (Ljava/lang/String;[Ljava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_javaJNITest_stringManipulator
        (JNIEnv *env, jobject obj, jstring str, jobjectArray strObj1) {
    std::string s = env->GetStringUTFChars(str, NULL);
    std::cout << "NOW IN NATIVE STRING ENVIRONMENT!!" << std::endl;
    std::cout << "Your caller says: " << s << std::endl;
    std::cout << "Now iterating over the given string array." << std::endl;
    // iterate over
    for(int i = 0; i < env->GetArrayLength(strObj1); i++) {
        std::cout
                << env->GetStringUTFChars((jstring)env->GetObjectArrayElement(strObj1, (jsize)i), JNI_FALSE)
                << std::endl;
    }
    s.append("::::::THIS IS APPENDED TEXT!!!! WARNING!!! WARNING!!!! :)");
    return env->NewStringUTF(s.data());
}

/*
 * ==============IMPLEMENTATION=================
 * Class:     javaJNITest
 * Method:    heapAlloc
 * Signature: (J)V
 */
 JNIEXPORT void JNICALL Java_javaJNITest_heapAlloc
        (JNIEnv *env, jobject obj, jlong size) {
    char *p[10] = {NULL};
	int loop = 10;
	int count = 0;
	int i;
	while(count < 10) {
		p[count] = (char *)malloc(size);
		printf("mallloc(%d): pid=%u %p\n",count, getpid(), p[count]);
		if(p[count]) {
			memset(p[count], '0', size);
		}
		count++;
		sleep(1);
	}
	
	i = 0;
	while(i < count) {
		printf("free(%d): pid=%u %p\n",i, getpid(), p[i]);
		if(p[i]) {
			free(p[i]);
		}
		i++;
	}
}

/*
 * ==============IMPLEMENTATION=================
 * Class:     javaJNITest
 * Method:    mmapHooking
 * Signature: (J)V
*/
JNIEXPORT void JNICALL Java_javaJNITest_mmapHooking
        (JNIEnv *env, jobject obj, jlong size, jstring prio) {
	int flag = MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_POPULATE;
	
	const char *sprio = env->GetStringUTFChars(prio, NULL);
	if(!strncasecmp(sprio, "cxl", 3)){
		flag |= MAP_EXMEM;
		std::cout<<"MAP_EXMEM"<<std::endl;
	}
	
	for(int i = 0; i < 100; i++) {
		char *addr = static_cast<char*>(mmap(NULL, size, PROT_READ|PROT_WRITE, flag, -1, 0));
		if(addr == MAP_FAILED) {
			perror("mmap error");
			exit(1);
		}
		
		jchar one, zero;
		memset(addr, '1', size);
		one=*(addr+size/2);
		memset(addr, '0', size);
		zero=*(addr+size/2);
		std::cout<<"addr["<<(void *)addr<<"], one="<<one<<" zero="<<zero<<std::endl;
		sleep(1);
		
		std::cout<<"addr["<<(void *)addr<<"] "<<std::endl;
		if(munmap((void *)addr, size) == -1) {
			perror("munmap error");
			exit(1);
		}
		std::cout<<"munmap success"<<std::endl;
		sleep(1);
	}
}
