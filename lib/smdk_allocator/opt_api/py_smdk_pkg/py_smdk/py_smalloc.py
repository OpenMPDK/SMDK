import sys, os, ctypes
from _py_smdk import lib, ffi

SMDK_MEM_NORMAL = lib.SMDK_MEM_NORMAL
SMDK_MEM_EXMEM = lib.SMDK_MEM_EXMEM
MAP_EXMEM = 0x200000

class mem_obj:
    def __init__(self, mem_type, data=None, size=None):
        self.mem_type = mem_type
        if data is None:
            if size is None:
                raise ValueError("At least one argument of 'size' or 'data' must be passed.")
            else:
                self.size = size
        else:
            if size is not None:
                self.size = max(size, sys.getsizeof(data))
            else:
                self.size = sys.getsizeof(data)
        buf = lib.s_malloc(self.mem_type, self.size)
        if buf != ffi.NULL:
            self.buf = buf
        else:
           raise ValueError("mem_obj init fail.")
        if data is not None:
            self.__set_internal(data)

    def set(self, data, mem_type=None):
        if mem_type == None: #set default value
            mem_type = self.mem_type
        if data is not None: # self memory allocation & error case
            if mem_type != self.mem_type:
                # resize: always
                if hasattr(self, 'data'):
                    size_new = max(self.size, sys.getsizeof(data))
                else:
                    size_new = self.size
                self.resize(mem_type, size_new)
                self.__set_internal(data)
            else:
                # resize: when mem_obj has 'data' attribute, and getsizeof(data) is bigger than (old)self.size
                if hasattr(self, 'data'): #update case
                    if self.size < sys.getsizeof(data):
                        self.resize(self.mem_type, sys.getsizeof(data))
                self.__set_internal(data)

    def __set_internal(self, data):# only for private
        lib.memcpy(self.buf, ffi.cast("void*", id(data)), sys.getsizeof(data))
        data_id = ctypes.c_uint64(ffi.cast("uint64_t", self.buf)).value
        self.data = ctypes.cast(data_id, ctypes.py_object).value

    def get(self):
        return self.data

    def resize(self, mem_type, size):
        buf = lib.s_realloc(mem_type, self.buf, size)
        if buf != ffi.NULL:
            self.mem_type = mem_type
            self.size = size
            self.buf = buf
            if hasattr(self, 'data'): #update only when data is exist already
                data_id = ctypes.c_uint64(ffi.cast("uint64_t", self.buf)).value
                self.data = ctypes.cast(data_id, ctypes.py_object).value
        else:
            raise ValueError("mem_obj.resize() fail.")

    def free(self):
        if hasattr(self, 'buf'):
            lib.s_free_type(self.mem_type, self.buf)
        if hasattr(self, 'data'):
            del self.data

    def __del__(self):
        self.free()


class mem_obj_node:
    def __init__(self, mem_type, node, data=None, size=None):
        self.node = node
        self.mem_type = mem_type
        if data is None:
            if size is None:
                raise ValueError("At least one argument of 'size' or 'data' must be passed.")
            else:
                self.size = size
        else:
            if size is not None:
                self.size = max(size, sys.getsizeof(data))
            else:
                self.size = sys.getsizeof(data)
        buf = lib.s_malloc_node(self.mem_type, self.size, node.encode())
        if buf != ffi.NULL:
            self.buf = buf
        else:
           raise ValueError("mem_obj init fail.")
        if data is not None:
            self.__set_internal(data)

    def set(self, data):
        if data is not None: # self memory allocation & error case
            # resize: always
            if hasattr(self, 'data'):
                size_new = max(self.size, sys.getsizeof(data))
            else:
                size_new = self.size
            self.resize(size_new)
            self.__set_internal(data)

    def __set_internal(self, data):# only for private
        lib.memcpy(self.buf, ffi.cast("void*", id(data)), sys.getsizeof(data))
        data_id = ctypes.c_uint64(ffi.cast("uint64_t", self.buf)).value
        self.data = ctypes.cast(data_id, ctypes.py_object).value

    def get(self):
        return self.data

    def resize(self, size):
        old_size = self.size;
        old_buf = self.buf;
        buf = lib.s_malloc_node(self.mem_type, self.size, self.node.encode())
        if buf != ffi.NULL:
            lib.memcpy(buf, old_buf, size)
            self.size = size
            self.buf = buf
            if hasattr(self, 'data'): #update only when data is exist already
                data_id = ctypes.c_uint64(ffi.cast("uint64_t", self.buf)).value
                self.data = ctypes.cast(data_id, ctypes.py_object).value
            lib.s_free_node(self.mem_type, old_buf, old_size)
        else:
            raise ValueError("mem_obj.resize() fail.")

    def free(self):
        if hasattr(self, 'data'):
            del self.data
        if hasattr(self, 'buf'):
            lib.s_free_node(self.mem_type, self.buf, self.size)

    def __del__(self):
        self.free()


def s_stats_print(unit):
    unit = ffi.cast("char", str(unit))
    lib.s_stats_print(unit)

def s_stats_node_print(unit):
    unit = ffi.cast("char", str(unit))
    lib.s_stats_node_print(unit) 

def get_memsize_total(smdk_memtype):
    return lib.s_get_memsize_total(smdk_memtype)

def get_memsize_used(smdk_memtype):
    return lib.s_get_memsize_used(smdk_memtype)

def get_memsize_available(smdk_memtype):
    return lib.s_get_memsize_available(smdk_memtype)

def get_memsize_node_total(smdk_memtype, node):
    return lib.s_get_memsize_node_total(smdk_memtype, node)

def get_memsize_node_available(smdk_memtype, node):
    return lib.s_get_memsize_node_available(smdk_memtype, node)

def enable_node_interleave(nodes):
    lib.s_enable_node_interleave(nodes.encode())

def disable_node_interleave():
    lib.s_disable_node_interleave()
