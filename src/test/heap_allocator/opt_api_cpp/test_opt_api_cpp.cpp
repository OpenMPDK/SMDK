#include <iostream>
#include <string>
#include "test.h"
#include "smdk_opt_api.hpp"
#include <numa.h>

using namespace std;

class CPPTest
{
public:
	CPPTest(size_t size, int iter, smdk_memtype_t type, string name)
		: size(size), iter(iter), type(type), name(name) {}
	~CPPTest() {}
protected:
	void print_mem_stats(string txt)
	{
		SmdkAllocator& allocator = SmdkAllocator::get_instance();
		cout << txt << endl;
		cout << "\ttype: " << type << endl;
		cout << "\ttotal: " << allocator.get_memsize_total(type) << endl;
		cout << "\tused: " << allocator.get_memsize_used(type) << endl;
		cout << "\tavailable: " << allocator.get_memsize_available(type) << endl;
	}
	void print_start(void)
	{
		cout << "Test(" << name << ") starts" << endl;
	}
	void print_end(void)
	{
		cout << "Test(" << name << ") ends" << endl;
	}
	size_t size;
	int iter;
	smdk_memtype_t type;
	string name;
};

class Test1: public CPPTest
{
public:
	Test1(size_t size, int iter, smdk_memtype_t type, string name)
		: CPPTest(size, iter, type, name) {}
	void run(void)
	{
		print_start();

		void* malloc_buf, *calloc_buf, *posix_memalign_buf;
		SmdkAllocator& allocator = SmdkAllocator::get_instance();

		malloc_buf = allocator.malloc(type, size);
		assert_ptr_not_null(malloc_buf, "s_malloc allocation failure");
		allocator.free(malloc_buf);

		calloc_buf = allocator.calloc(type, 1, size);
		assert_ptr_not_null(calloc_buf, "s_calloc allocation failure");

		calloc_buf = allocator.realloc(type, calloc_buf, size*2);
		assert_ptr_not_null(calloc_buf, "s_realloc allocation failure");
		allocator.free(type, calloc_buf);

		int ret = allocator.posix_memalign(type, &posix_memalign_buf, sizeof(void*), size);
		assert_d_eq(ret, 0, "s_posix_memalign allocation failure");
		allocator.free(posix_memalign_buf);

		allocator.stats_print('K');

		print_end();
	}
};

class Test2: public CPPTest
{
public:
	Test2(size_t size, int iter, smdk_memtype_t type, string name)
		: CPPTest(size, iter, type, name) {}
	void run(void)
	{
		print_start();
		SmdkAllocator& allocator = SmdkAllocator::get_instance();

		void* buf;
		for(int i = 0; i < iter; i++){
			buf = allocator.malloc(type, size);
			assert_ptr_not_null(buf, "s_malloc allocation failure");
			memset(buf, 0, size);
			allocator.free(type, buf);
		}

		allocator.stats_print('M');

		print_end();
	}
};

class Test3: public CPPTest
{
public:
	Test3(size_t size, int iter, smdk_memtype_t type, string name)
		: CPPTest(size, iter, type, name) {}
	void run(void)
	{
		print_start();
		SmdkAllocator& allocator = SmdkAllocator::get_instance();

		void** buf = new void*[iter];
		assert_ptr_not_null(buf, "malloc allocation failure");
		print_mem_stats("Before Malloc");

		for(int i = 0; i < iter; i++){
			buf[i] = allocator.malloc(type, size);
			assert_ptr_not_null(buf, "s_malloc allocation failure");
			memset(buf[i], 0, size);
		}
		print_mem_stats("After Malloc");

		for(int i = 0; i < iter; i++){
			allocator.free(type, buf[i]);
		}
		print_mem_stats("After Free");

		delete buf;

		allocator.stats_print('G');

		print_end();
	}
};

class Test4: public CPPTest
{
public:
	Test4(size_t size, int iter, smdk_memtype_t type, string name, string nodes)
		: CPPTest(size, iter, type, name)
	{
		this->nodes = nodes;
	}
	void run(void)
	{
		print_start();
		SmdkAllocator& allocator = SmdkAllocator::get_instance();

		void* buf;
		for(int i = 0; i < iter; i++){
			buf = allocator.malloc_node(type, size, nodes);
			assert_ptr_not_null(buf, "s_malloc allocation failure");
			memset(buf, 0, size);
			allocator.free_node(type, buf, size);
		}

		allocator.stats_print('M');

		print_end();
	}
	string nodes;
};

class Test5: public CPPTest
{
public:
	Test5(size_t size, int iter, smdk_memtype_t type, string name, string nodes)
		: CPPTest(size, iter, type, name)
	{
		this->nodes = nodes;
	}
	void run(void)
	{
		print_start();
		SmdkAllocator& allocator = SmdkAllocator::get_instance();

		void* buf;
		allocator.enable_node_interleave(nodes);
		for(int i = 0; i < iter; i++){
			buf = allocator.malloc(type, size);
			assert_ptr_not_null(buf, "s_malloc allocation failure");
			memset(buf, 0, size);
			allocator.free(buf);
		}
		allocator.disable_node_interleave();

		allocator.stats_print('M');

		print_end();
	}
	string nodes;
};

int main(void){
	size_t size = 1024;
	int iter = 10000000;
	string node_range = "0";
	if(numa_max_node() != 0)
		node_range += "-" + to_string(numa_max_node());
	string node_max = to_string(numa_max_node());
	smdk_memtype_t type = SMDK_MEM_NORMAL;

	Test1 tc1(size, iter, type, "basic functional test");
	tc1.run();

	Test2 tc2(size, iter, type, "malloc-free test");
	tc2.run();

	Test3 tc3(size, iter, type, "malloc-memstat-free test");
	tc3.run();

	Test4 tc4(size, iter, type, "alloc_on_nodes test", node_range);
	tc4.run();

	Test5 tc5(size, iter, type, "enable_node_interleave test", node_max);
	tc5.run();

	return 0;
}
