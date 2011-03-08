CFLAGS=-std=c99 -Wall -pedantic -D_GNU_SOURCE

PROGRAM=test-discard
SRC=test-discard.c
PROGRAM_PROFILE=test-discard.profile

ALL: $(PROGRAM)

$(PROGRAM): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@

profile:
	$(CC) $(CFLAGS) $(SRC) -pg -o $(PROGRAM_PROFILE)

archive: tar bzip

tar:
	git archive --format=tar --prefix=test-discard/ HEAD -o archive.tar

bzip:
	bzip2 archive.tar


clean:
	rm -rf *.o $(PROGRAM) $(PROGRAM_PROFILE) *.dat *.ps *.pdf
