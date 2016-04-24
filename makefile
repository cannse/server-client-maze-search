# Server/client maze search makefile
CC = gcc
CFLAGS = -g -Wall -pedantic -std=c11 -lm -pthread `pkg-config --cflags --libs gtk+-2.0`

amazing: AMStartup.c AMStartup.o amazing.h
	$(CC) $(CFLAGS) -o $@ AMStartup.c amazing.h

clean:
	rm -f amazing
	rm -f *~
	rm -f *#
	rm -f *.o
	rm -f core.*
	rm -f src/*~
	rm -f src/*#
	rm -f src/*.o
	rm -f src/core.*
