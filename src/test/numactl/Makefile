CC=gcc
CFLAGS=-W -Wall
APP=simple_malloc
OBJS=simple_malloc.o

all: $(APP)

$(APP): $(OBJS) 
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm *.o $(APP)
