CC=gcc
CFLAG1=-fPIC -std=gnu99
CFLAG2=-shared
CFLAG3=-lrt -L. -lBeMa

all: genex clean

clean:
	rm -f main.o

geno: 
	$(CC) $(CFLAG1) -c main.c

genlib: geno
	$(CC) $(CFLAG2) main.o -o libBeMa.so

genex: genlib
	$(CC) testBeMa.c $(CFLAG3)
	export LD_LIBRARY_PATH=./:$LD_LIBRARY_PATH
 
