
CC="g++"
OPTIONS="-DPIC -g"

.PHONY: build clean


build: piper

clean:
	rm -rf piper libasound_module_pcm_piper.so
	rm -rf file.o main.o pipe.o plugin.o timer.o tokenbucket.o

piper: file.o main.o pipe.o timer.o tokenbucket.o
	$(CC) -o piper main.o pipe.o timer.o tokenbucket.o file.o

libasound_module_pcm_piper.so: file.o pipe.o plugin.o timer.o
	$(CC) -shared -Wl,-soname,libasound_module_pcm_piper.so -o libasound_module_pcm_piper.so file.o pipe.o plugin.o timer.o -lasound

file.o: file.cpp buffer.hpp exception.hpp file.hpp transfer.hpp
	$(CC) -std=c++11 -Wall -fPIC $(OPTIONS) -c file.cpp

main.o: main.cpp buffer.hpp exception.hpp file.hpp mmap.hpp pipe.hpp preamble.hpp timer.hpp timestamp.hpp tokenbucket.hpp
	$(CC) -std=c++11 -Wall -fPIC $(OPTIONS) -c main.cpp

pipe.o: pipe.cpp buffer.hpp exception.hpp file.hpp mmap.hpp pipe.hpp transfer.hpp
	$(CC) -std=c++11 -Wall -fPIC $(OPTIONS) -c pipe.cpp

plugin.o: plugin.cpp buffer.hpp exception.hpp file.hpp mmap.hpp pipe.hpp preamble.hpp timer.hpp timestamp.hpp tokenbucket.hpp
	$(CC) -std=c++11 -Wall -fPIC $(OPTIONS) -c plugin.cpp

timer.o: timer.cpp exception.hpp timer.hpp
	$(CC) -std=c++11 -Wall -fPIC $(OPTIONS) -c timer.cpp

tokenbucket.o: tokenbucket.cpp exception.hpp timer.hpp tokenbucket.hpp
	$(CC) -std=c++11 -Wall -fPIC $(OPTIONS) -c tokenbucket.cpp


