CFLAGS=-std=c99 -Wall -pedantic -D_GNU_SOURCE

PROGRAM=test-discard
SRC=test-discard.c

ALL: $(PROGRAM)

$(PROGRAM): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@

archive: tar bzip

tar:
	git archive --format=tar --prefix=test-discard/ HEAD -o archive.tar

bzip:
	bzip2 archive.tar


clean:
	rm -rf *.o test-discard *.dat *.ps *.pdf
