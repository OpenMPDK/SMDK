from cffi import FFI

ffi=FFI()

ffi.cdef("""
typedef enum {
    SMDK_MEM_NORMAL,
    SMDK_MEM_EXMEM
} smdk_memtype_t;
void *s_malloc(smdk_memtype_t type, size_t size);
void *s_realloc(smdk_memtype_t type, void *ptr, size_t size);
void s_free_type(smdk_memtype_t type, void *ptr);
void s_stats_print(char unit);
void s_stats_node_print(char unit);
size_t s_get_memsize_total(smdk_memtype_t type);
size_t s_get_memsize_used(smdk_memtype_t type);
size_t s_get_memsize_available(smdk_memtype_t type);
void *s_malloc_node(smdk_memtype_t type, size_t size, char* nodes);
void s_free_node(smdk_memtype_t type, void* mem, size_t size);
void s_enable_node_interleave(char* nodes);
void s_disable_node_interleave(void);
size_t s_get_memsize_node_total(smdk_memtype_t type, int node);
size_t s_get_memsize_node_available(smdk_memtype_t type, int node);
void* memcpy (void* dest, const void* source, size_t num);
""")

ffi.set_source("_py_smdk",
        """
        #include "smdk_opt_api.h"
        #include <string.h>
        """,
        include_dirs=["../../include"],
        library_dirs=["../../../lib"], #when you copy libsmalloc.a to this directory
        libraries=["smalloc"],
        extra_link_args=["-lnuma"],
        )

if __name__ == "__main__":
    ffi.compile(verbose=True)


