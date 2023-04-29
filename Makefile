CC=gcc

uns: uns.o
	$(CC) -o uns uns.o -pthread

uns.o : uns.c
	$(CC) -c uns.c

clean :
	rm -f uns uns.o
