import sys, os, time
from py_smdk import py_smalloc

memtype = py_smalloc.SMDK_MEM_NORMAL
objsize = 4 * 1024

my_int = 1
my_float = 3.45
my_str = "hello smdk"
my_bytes = b"123456789"
my_list = [1, 2, 3, 4]
my_tuple = (1, 2, 3, 4)
my_dictionary = {"key1":"value1", "key2":"value2", "key3":"value3"}
my_arr = [my_int, my_float, my_str, my_bytes, my_list, my_tuple, my_dictionary]


#test0: how to use
#0-1 get smdk mem_obj by data
memtype = py_smalloc.SMDK_MEM_EXMEM
mydata = "hello smdk"
smdk_obj = py_smalloc.mem_obj(memtype, mydata)
#print(smdk_obj.data) #or print(smdk_obj.get())
mydata_changed = "nice to meet you!"
smdk_obj.set(mydata_changed, memtype)
#print(smdk_obj.data) #or print(smdk_obj.get())
del smdk_obj

#0-2 get smdk mem_obj by size
memtype = py_smalloc.SMDK_MEM_EXMEM
mydata = "hello smdk"
smdk_obj = py_smalloc.mem_obj(memtype, size=sys.getsizeof(mydata))
smdk_obj.set(mydata)
#print(smdk_obj.data) #or print(smdk_obj.get())
mydata_changed = "nice to meet you!"
smdk_obj.resize(memtype, sys.getsizeof(mydata_changed))
smdk_obj.set(mydata_changed)
#print(smdk_obj.data) #or print(smdk_obj.get())
del smdk_obj
print("test 0 done")


#test1: basic alloc operation
for raw_data in my_arr:
    smdk_obj = py_smalloc.mem_obj(memtype, size=objsize)
    smdk_obj.set(raw_data)
    assert type(smdk_obj.data) is type(raw_data), "test1: type difference between raw data and copied one."
    del smdk_obj
print("test 1 done")


#test2: exception cases: invalid init argument
error_count = 0
try:
    smdk_obj = py_smalloc.mem_obj(3, size=objsize)
except:
    error_count += 1
try:
    smdk_obj = py_smalloc.mem_obj(memtype, size=-1)
except:
    error_count += 1
try:
    smdk_obj = py_smalloc.mem_obj(memtype)
except:
    error_count += 1
assert error_count == 3, "test2: errors did not occur for wrong arguments(error occured:{0}).".format(error_count)
print("test 2 done")


#test3: resize(realloc)
raw_data = 'hello smdk'
smdk_obj = py_smalloc.mem_obj(memtype, size=objsize)
smdk_obj.set(raw_data)
mem_used_old = py_smalloc.get_memsize_used(memtype)
smdk_obj.resize(memtype, objsize * 1024)
mem_used_new = py_smalloc.get_memsize_used(memtype)
assert mem_used_new > mem_used_old, "test3: realloc did not work properly."
assert raw_data == smdk_obj.data, "test3: the original data was broken during realloc."
del smdk_obj
print("test 3 done")


#test4: alloc memory then call get_memsize_used
niter = 25 * 1024
arr_smdk_obj = []
m_1 = py_smalloc.get_memsize_used(memtype)

for i in range(niter):
    arr_smdk_obj.append(py_smalloc.mem_obj(memtype, size=objsize))
m_2 = py_smalloc.get_memsize_used(memtype)

for obj in arr_smdk_obj:
    obj.free()
    del obj
m_3 = py_smalloc.get_memsize_used(memtype)
assert m_1 <= m_3 < m_2, "test4: obj did not freed properly.m1={0}, m2={1}, m3={2}".format(m_1, m_2, m_3)
print("test 4 done")


#test5: set data to smdk mem_obj repeatedly without calling resize method
smdk_obj = py_smalloc.mem_obj(memtype, "start")
for raw_data in my_arr:
    smdk_obj.set(raw_data)
    assert type(smdk_obj.data) is type(raw_data), "test5: type difference between raw data and copied one."
smdk_obj.free()
print("test 5 done")


#test6: resize smdk mem_obj and set data repeatedly
smdk_obj = py_smalloc.mem_obj(memtype, "start")
for raw_data in my_arr:
    smdk_obj.resize(memtype, sys.getsizeof(raw_data))
    smdk_obj.set(raw_data)
    assert type(smdk_obj.data) is type(raw_data), "test6: type difference between raw data and copied one."
smdk_obj.free()
print("test 6 done")


#test7: save(set) data by changing memory types repeatedly
arr_memtype = [py_smalloc.SMDK_MEM_NORMAL, py_smalloc.SMDK_MEM_EXMEM]
raw_data = "hello smdk!"
smdk_obj = py_smalloc.mem_obj(memtype, size=100)
for i in range(100):
    smdk_obj.set(raw_data, arr_memtype[i % 2])
    assert smdk_obj.data == raw_data, "test7 : the original data was broken"
smdk_obj.free()
print("test 7 done")


#test8: basic data types of python
#8-1_list
smdk_obj = py_smalloc.mem_obj(memtype, my_list)
for i in range(4): #changing values
    smdk_obj.data[i]=i
#print(smdk_obj.data)
print("len obj = %d"%len(smdk_obj.data))
smdk_obj.data.pop() #use pop
print("pop data =", smdk_obj.data)
print("slicing list", smdk_obj.data[:2]) # use indexing
smdk_obj.data.sort(reverse=True) # use sorting
print("sorted", smdk_obj.data)
idx = smdk_obj.data.index(2) # find index
print("index of data '2'=%d"%idx)
smdk_obj.free()
print("list usage checked")

#8-2_tuple
smdk_obj = py_smalloc.mem_obj(memtype, my_tuple)
#print(smdk_obj.data)
print("3rd item is", smdk_obj.data[2])
print("slicing tuple", smdk_obj.data[0:2])
print("len tuple = %d"%len(smdk_obj.data))
smdk_obj.data = smdk_obj.data + (5,6,7,8)
print("add (5,6,7,8) tuple = ", smdk_obj.data)
smdk_obj.free()
print("tuple usage checked")

#8-3_dictionary
smdk_obj_dic = py_smalloc.mem_obj(memtype, my_dictionary)
#print(smdk_obj_dic.data)
for key, val in smdk_obj_dic.data.items():
    print(key, val)
#print(smdk_obj_dic.data['key1'])
smdk_obj_dic.data['key1'] = 1
smdk_obj_dic.data['key2'] = 2
smdk_obj_dic.data['key3'] = 3
#print(smdk_obj_dic.data)
smdk_obj_dic.free()
print("Dictionary usage checked")

#8-4_set
set1 = set([0, 1, 2, 3])
set2 = set([2, 3, 4, 5, 6])
smdk_obj1 = py_smalloc.mem_obj(memtype, set1)
smdk_obj2 = py_smalloc.mem_obj(memtype, set2)
#print(smdk_obj1.data, smdk_obj2.data)
print("Intersection set", smdk_obj1.data & smdk_obj2.data)
print("Union set", smdk_obj1.data | smdk_obj2.data)
print("Difference set", smdk_obj1.data - smdk_obj2.data)
smdk_obj1.free()
smdk_obj2.free()
print("Set usage checked")
print("test 8 done")

#test9: metadata API
py_smalloc.s_stats_print('G')
py_smalloc.s_stats_node_print('M')
print("test 9 done")

#test10: mem_obj_nodes
memtype = py_smalloc.SMDK_MEM_EXMEM
smdk_obj_node = py_smalloc.mem_obj_node(memtype, "0", "SMDK")
#print(smdk_obj_node.data, smdk_obj_node.buf)
smdk_obj_node2 = py_smalloc.mem_obj_node(memtype, "0", size=1024)
smdk_obj_node2.set("SMDK")
#print(smdk_obj_node2.data, smdk_obj_node2.buf)
smdk_obj_node2.set("Hello SMDK")
#print(smdk_obj_node2.data, smdk_obj_node2.buf)
my_set = [1,2,3,4]
smdk_obj_node2.set(my_set)
#print(smdk_obj_node2.data, smdk_obj_node2.buf)
del smdk_obj_node
del smdk_obj_node2
print("test 10_1 done")

#test10_2: exception case: not available node variable
error_count=0
try:
    smdk_obj_node = py_smalloc.mem_obj_node(memtype, "99", size=1024)
    del smdk_obj_node
except:
    error_count += 1
assert error_count == 1, "error did not occur for wrong argument"
print("test 10_2 done")

#test10_3: set data without resize
smdk_obj_node = py_smalloc.mem_obj_node(memtype, "0", 777)
for raw_data in my_arr:
    smdk_obj_node.set(raw_data)
    assert type(smdk_obj_node.data) == type(raw_data), "type difference between raw data and copied one"
del smdk_obj_node
print("test 10_3 done")


#test10_4: test with resize
smdk_obj_node = py_smalloc.mem_obj_node(memtype, "0", "start")
for raw_data in my_arr:
    smdk_obj_node.resize(sys.getsizeof(raw_data))
    smdk_obj_node.set(raw_data)
    assert type(smdk_obj_node.data) == type(raw_data),"type difference between raw data and copied one"
del smdk_obj_node
print("test 10_4 done")

#test10_5: test python types
#list
smdk_obj_node = py_smalloc.mem_obj_node(memtype, "0", my_list)
for i in range(4):
    smdk_obj_node.data[i]=i
print("changed list=", smdk_obj_node.data)
print("slicing list", smdk_obj_node.data[1:4])
smdk_obj_node.data.sort(reverse=True)
print("sorted list", smdk_obj_node.data)
del smdk_obj_node
print("list_test done")
print("test 10 done")

#test 11: metadata API:get_memsize_node // enable|disable_node_interleave
print(py_smalloc.get_memsize_node_total(py_smalloc.SMDK_MEM_NORMAL, 0))
print(py_smalloc.get_memsize_node_total(py_smalloc.SMDK_MEM_NORMAL, 1))
print(py_smalloc.get_memsize_node_available(py_smalloc.SMDK_MEM_NORMAL, 0))
print(py_smalloc.get_memsize_node_available(py_smalloc.SMDK_MEM_NORMAL, 1))
py_smalloc.enable_node_interleave("0")
py_smalloc.enable_node_interleave("1")
py_smalloc.enable_node_interleave("0,1")
py_smalloc.enable_node_interleave("0-1")
py_smalloc.enable_node_interleave("0,2-3")
py_smalloc.disable_node_interleave()
print("test 11 done")
