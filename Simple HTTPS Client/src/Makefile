all: myweb.o
	clang++ -Wall -std=c++11 myweb.cpp -l ssl -l crypto -o ../bin/myweb
	rm ./myweb.o

clean:
	rm ../bin/myweb ../bin/output.dat
