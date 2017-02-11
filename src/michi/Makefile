CFLAGS= -DNDEBUG -O3 -msse4.1 -fshort-enums -Wall -Wno-char-subscripts

all: michi.o
	gcc $(CFLAGS) -std=c99 -o michi michi.o -lm

%.o: %.c
	gcc $(CFLAGS) -c -std=c99 $<

clean:
	rm -rf michi.o michi tests/output tests/michi.log michi.log
