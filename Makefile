CFLAGS=-std=c99 -Wall -pedantic -D_GNU_SOURCE

PROGRAM=test-discard
SRC=test-discard.c

ALL: $(PROGRAM)

$(PROGRAM): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@

clean:
	rm -rf *.o test-discard *.dat *.ps *.pdf
