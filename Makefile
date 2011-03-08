CFLAGS=-Wall -D_GNU_SOURCE

PROGRAM=test-discard
SRC=test-discard.c
PROGRAM_PROFILE=test-discard.profile

LIB_DIR=libs
LIB_OBJS=$(LIB_DIR)/rbtree.o

ALL: $(LIB_OBJS) $(PROGRAM)

$(PROGRAM): $(SRC)
	$(CC) $(CFLAGS) $(SRC) $(LIB_OBJS) -g -o $@

profile: $(LIB_OBJS)
	$(CC) $(CFLAGS) $(SRC) $(LIB_OBJS) -pg -o $(PROGRAM_PROFILE)

archive: tar bzip

tar:
	git archive --format=tar --prefix=test-discard/ HEAD -o archive.tar

bzip:
	bzip2 archive.tar


clean:
	rm -rf $(LIB_DIR)/*.o *.o $(PROGRAM) $(PROGRAM_PROFILE) *.dat *.ps *.pdf
