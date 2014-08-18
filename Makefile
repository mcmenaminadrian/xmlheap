default: all

all: xmlheap

debug: debugxmlheap

clean:
	rm -f *.o

xmlheap: xmlheap.o
	gcc -O2 -o xmlheap -Wall xmlheap.o -lexpat

xmlheap.o: xmlheap.c
	gcc -O2 -o xmlheap.o -c -Wall xmlheap.c

debugxmlheap: dxmlheap.o
	gcc -g -O0 -o xmlheap -Wall dxmlheap.o -lexpat

dxmlheap.o: xmlheap.c
	gcc -g -O0 -o dxmlheap.o -c -Wall xmlheap.c

