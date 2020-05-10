all: myclient.o myserver.o
	clang++ -Wall -std=c++11 myclient.cpp -l readline -o ../bin/myclient
	clang++ -Wall -std=c++11 myserver.cpp -o ../bin/myserver
	rm myclient.o myserver.o

server: myserver.o
	clang++ -Wall -std=c++11 myserver.cpp -o ../bin/myserver
	rm myserver.o

client: myclient.o
	clang++ -Wall -std=c++11 myclient.cpp -l readline -o ../big/myclient
	rm myclient.o

clean:
	rm ../bin/myclient ../bin/myserver
