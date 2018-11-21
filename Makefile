

.PHONY: build


build: piper

piper: file.o main.o pipe.o timer.o tokenbucket.o
	g++ -o piper main.o pipe.o timer.o tokenbucket.o file.o

file.o: file.cpp file.hpp exception.hpp buffer.hpp transfer.hpp
	g++ -std=c++11 -Wall -c file.cpp

main.o: main.cpp exception.hpp exception.hpp buffer.hpp file.hpp mmap.hpp pipe.hpp timer.hpp tokenbucket.hpp
	g++ -std=c++11 -Wall -c main.cpp

pipe.o: pipe.cpp pipe.hpp exception.hpp buffer.hpp file.hpp mmap.hpp transfer.hpp
	g++ -std=c++11 -Wall -c pipe.cpp

timer.o: timer.cpp timer.hpp exception.hpp
	g++ -std=c++11 -Wall -c timer.cpp

tokenbucket.o: tokenbucket.cpp tokenbucket.hpp exception.hpp timer.hpp
	g++ -std=c++11 -Wall -c tokenbucket.cpp


