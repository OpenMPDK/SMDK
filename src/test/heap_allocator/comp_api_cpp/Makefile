CXX=g++
CXXFLAGS=-W -Wall

TC1=test

all: $(TC1)

$(TC1): $(TC1).o
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS) $(LIBS) -lstdc++

clean:
	rm -f *.o $(TC1)
