CC := gcc 

TC1 := test_syscall

all: $(TC1)

$(TC1): $(TC1).o
	$(CC) -Wall -o $@ $^

clean:
	rm -f *.o $(TC1)
