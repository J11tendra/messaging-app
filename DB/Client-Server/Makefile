CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread
LDFLAGS = -lsqlite3 -lpthread

# Targets
all: server client

# Object files
protocol.o: protocol.cpp protocol.h
	$(CXX) $(CXXFLAGS) -c protocol.cpp -o protocol.o

db.o: db.cpp db.h protocol.h
	$(CXX) $(CXXFLAGS) -c db.cpp -o db.o

# Binaries (Uncomment when server.cpp and client.cpp are added by team)
server: server.cpp protocol.o db.o
	$(CXX) $(CXXFLAGS) server.cpp protocol.o db.o $(LDFLAGS) -o server

client: client.cpp protocol.o
	$(CXX) $(CXXFLAGS) client.cpp protocol.o $(LDFLAGS) -o client

# Test target
test_db: test_db.cpp db.o protocol.o
	$(CXX) $(CXXFLAGS) test_db.cpp db.o protocol.o $(LDFLAGS) -o test_db
	./test_db

clean:
	rm -f *.o server client server.db test_db test_suite.db
