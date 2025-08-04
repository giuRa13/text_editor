all: kilo

kilo: kilo.c
	gcc -o kilo.o kilo.c -Wall -Wextra -pedantic -std=c99

run:
	./kilo.o

clean: 
	rm kilo.o
