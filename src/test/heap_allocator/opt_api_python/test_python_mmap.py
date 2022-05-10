import mmap
import os, sys, time
from py_smdk import py_smalloc

MAP_EXMEM = py_smalloc.MAP_EXMEM

filename = "./4M.dummy"
fileno = -1 #for MAP_ANON
length = 4*1024*1024 #4MB
num = 1000
flags = mmap.MAP_ANON | mmap.MAP_PRIVATE | MAP_EXMEM #mmap.MAP_POPULATE from 3.10
prot = mmap.PROT_WRITE | mmap.PROT_READ

try:
    f = open("./4M.dummy", "r")
except:
    print("You need to make dummy file first (e.g.$dd if=/dev/zero of=4M.dummy bs=1M count=4)")
    sys.exit()

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
