import mmap
import os, sys, time
from py_smdk import py_smalloc

if sys.argv[1] == "exmem":
    MEMTYPE_FLAG = py_smalloc.MAP_EXMEM
else:
    MEMTYPE_FLAG = py_smalloc.MAP_NORMAL

filepath = os.path.dirname(os.path.realpath(__file__))
filename = filepath + "/4M.dummy"
fileno = -1 #for MAP_ANON
length = 4*1024*1024 #4MB
num = 1000
flags = mmap.MAP_ANON | mmap.MAP_PRIVATE | MEMTYPE_FLAG #cf.mmap.MAP_POPULATE from 3.10
prot = mmap.PROT_WRITE | mmap.PROT_READ

try:
    f = open(filename, "r")
except:
    print("You need to make dummy file first (e.g.$dd if=/dev/zero of=%s/4M.dummy bs=1M count=4)" % filepath)
    sys.exit(2)

data = f.read().encode() #4MB data chunk

mm_array = []
for i in range(num):
    mm_array.append(mmap.mmap(fileno, length, flags, prot))
    mm_array[i].write(data) #4MB data write
print("mmap done")
time.sleep(5)

for i in range(num):
    mm_array[i].close()

print("mmap close done")
time.sleep(5)
