CC=gcc
CFLAGS=-W -Wall -I.
LIBS += -lpthread

APP=test_lazy_alloc
OBJS=test_lazy_alloc.o

all: $(APP)

$(APP): $(OBJS) 
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	rm *.o $(APP)
