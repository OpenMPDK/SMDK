CC=gcc
CFLAGS=-W -Wall

TC1=test

all: $(TC1)

$(TC1): $(TC1).o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LIBS) -lstdc++

clean:
	rm -f *.o $(TC1)
