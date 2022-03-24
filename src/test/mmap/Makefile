CC=gcc
CFLAGS=-W -Wall
APP=test_mmap_cxl
OBJS=test_mmap_cxl.o

all: $(APP)

$(APP): $(OBJS) 
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm *.o $(APP)
