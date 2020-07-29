all: A B

A: A.c
	gcc -o A A.c

B: B.c
	gcc -o B B.c -lm

clean:
	rm -f A A.o B B.o