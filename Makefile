
CC="g++"
OPTIONS="-DPIC -g"

.PHONY: build clean


build: piper

clean:
	rm -rf piper libasound_module_pcm_piper.so
	rm -rf file.o main.o pipe.o plugin.o timer.o tokenbucket.o transport.o

piper: file.o main.o pipe.o timer.o tokenbucket.o transport.o
	$(CC) -g -o piper file.o main.o pipe.o timer.o tokenbucket.o transport.o -lasound

libasound_module_pcm_piper.so: file.o pipe.o plugin.o timer.o transport.o libasound_module_pcm_piper.version
	$(CC) -shared -Wl,-soname,libasound_module_pcm_piper.so -Wl,--version-script=libasound_module_pcm_piper.version -o libasound_module_pcm_piper.so file.o pipe.o plugin.o timer.o transport.o -lasound

file.o: file.cpp buffer.hpp exception.hpp file.hpp transfer.hpp
	$(CC) -std=c++11 -Wall -fPIC $(OPTIONS) -c file.cpp

main.o: main.cpp buffer.hpp exception.hpp file.hpp pipe.hpp timer.hpp timestamp.hpp tokenbucket.hpp transfer.hpp transport.hpp
	$(CC) -std=c++11 -Wall -fPIC $(OPTIONS) -c main.cpp

pipe.o: pipe.cpp buffer.hpp exception.hpp file.hpp pipe.hpp transfer.hpp transport.hpp
	$(CC) -std=c++11 -Wall -fPIC $(OPTIONS) -c pipe.cpp

plugin.o: plugin.cpp buffer.hpp exception.hpp file.hpp pipe.hpp timer.hpp timestamp.hpp tokenbucket.hpp transfer.hpp transport.hpp
	$(CC) -std=c++11 -Wall -fPIC $(OPTIONS) -c plugin.cpp

timer.o: timer.cpp exception.hpp timer.hpp timestamp.hpp
	$(CC) -std=c++11 -Wall -fPIC $(OPTIONS) -c timer.cpp

tokenbucket.o: tokenbucket.cpp exception.hpp timer.hpp timestamp.hpp tokenbucket.hpp
	$(CC) -std=c++11 -Wall -fPIC $(OPTIONS) -c tokenbucket.cpp

transport.o: transport.cpp buffer.hpp exception.hpp file.hpp transfer.hpp transport.hpp
	$(CC) -std=c++11 -Wall -fPIC $(OPTIONS) -c transport.cpp


