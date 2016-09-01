all: mylib.so server

mylib.o: mylib.c
	gcc -Wall -fPIC -DPIC -c -I../include mylib.c -L../lib mylib.c -ldirtree

mylib.so: mylib.o
	ld -lc -shared -o mylib.so mylib.o -ldl

server: server.c
	gcc -fPIC -DPIC -I../include -o server server.c -g -L../lib -ldirtree

clean:
	rm -f *.o *.so server

