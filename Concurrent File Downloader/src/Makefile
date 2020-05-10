all: myclient.o myserver.o
	clang++ -Wall -std=c++11 -pthread myclient.cpp -o ../bin/myclient
	clang++ -Wall -std=c++11 myserver.cpp -o ../bin/myserver
	rm myclient.o myserver.o
clean:
	rm ../bin/myclient ../bin/myserver
