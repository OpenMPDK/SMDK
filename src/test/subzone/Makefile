CC=gcc
CFLAGS=-W -Wall
LIBS+=-lpthread

APP1=test_multithread_malloc
OBJS1=test_multithread_malloc.o

APP2=test_random_malloc
OBJS2=test_random_malloc.o


all: $(APP1) $(APP2)

$(APP1): $(OBJS1)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(APP2): $(OBJS2)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -rf *.o $(APP1) $(APP2)
