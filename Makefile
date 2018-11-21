

.PHONY: build


build: piper

piper: main.o pipe.o tokenbucket.o file.o
	g++ -o piper main.o pipe.o tokenbucket.o file.o

main.o: main.cpp exception.hpp exception.hpp buffer.hpp file.hpp mmap.hpp pipe.hpp tokenbucket.hpp
	g++ -std=c++11 -Wall -c main.cpp

pipe.o: pipe.cpp pipe.hpp exception.hpp buffer.hpp file.hpp mmap.hpp
	g++ -std=c++11 -Wall -c pipe.cpp

timer.o: timer.cpp timer.hpp exception.hpp
	g++ -std=c++11 -Wall -c timer.cpp

tokenbucket.o: tokenbucket.cpp tokenbucket.hpp exception.hpp
	g++ -std=c++11 -Wall -c tokenbucket.cpp

file.o: file.cpp file.hpp exception.hpp buffer.hpp
	g++ -std=c++11 -Wall -c file.cpp



