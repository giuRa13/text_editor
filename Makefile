all: kilo

kilo: kilo.c
	gcc -o kilo kilo.c -Wall -Wextra -pedantic -std=c99

run:
	./kilo

clean: 
	rm kilo
