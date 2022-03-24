#include <assert.h>
#include "smdk_opt_api.h"

class SmdkAllocator
{
public:
    static SmdkAllocator& get_instance()
    {
        static SmdkAllocator s;
        return s;
    }
    void *malloc(smdk_memtype_t type, size_t size)
    {
        return s_malloc(type, size);
    }
    void *calloc(smdk_memtype_t type, size_t num, size_t size)
    {
        return s_calloc(type, num, size);
    }
    void *realloc(smdk_memtype_t type, void *ptr, size_t size)
    {
        return s_realloc(type, ptr, size);
    }
    int posix_memalign(smdk_memtype_t type, void **memptr, size_t alignment, size_t size)
    {
        return s_posix_memalign(type, memptr, alignment, size);
    }
    void free(void *ptr)
    {
        return s_free(ptr);
    }
    void free(smdk_memtype_t type, void *ptr)
    {
        return s_free_type(type, ptr);
    }
    size_t get_memsize_total(smdk_memtype_t type)
    {
        return s_get_memsize_total(type);
    }
    size_t get_memsize_used(smdk_memtype_t type)
    {
        return s_get_memsize_used(type);
    }
    size_t get_memsize_available(smdk_memtype_t type)
    {
        return s_get_memsize_available(type);
    }
    void stats_print(void)
    {
        s_stats_print();
    }
private:
    SmdkAllocator() = default;
    SmdkAllocator(const SmdkAllocator& ref) = delete;
    SmdkAllocator& operator=(const SmdkAllocator& ref) = delete;
    ~SmdkAllocator() = default;
};
