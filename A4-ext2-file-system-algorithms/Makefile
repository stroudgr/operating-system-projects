PROGS=ext2_mkdir ext2_cp ext2_ln ext2_rm ext2_restore ext2_checker

all: $(PROGS)

ext2_cp: ext2_cp.o ext2_help.o
	gcc -Wall -o $@ $^

ext2_mkdir: ext2_mkdir.o ext2_help.o
	gcc -Wall -o $@ ext2_mkdir.o ext2_help.o

ext2_ln: ext2_ln.o ext2_help.o
	gcc -Wall -o $@ $^

ext2_rm: ext2_rm.o ext2_help.o
	gcc -Wall -o $@ ext2_rm.o ext2_help.o

ext2_restore: ext2_restore.o ext2_help.o
	gcc -Wall -o $@ ext2_restore.o ext2_help.o

ext2_checker: ext2_checker.o ext2_help.o
	gcc -Wall -o $@ ext2_checker.o ext2_help.o

# TODO delete before submitting
ext2_test: ext2_test.o ext2_help.o
	gcc -Wall -o $@ ext2_test.o ext2_help.o

ext2_ls: ext2_ls.o ext2_help.o
		gcc -Wall -o $@ ext2_ls.o ext2_help.o

%.o: %.c ext2_help.h
	gcc -Wall -g -c $<

clean:
	rm -f *.o $(PROGS)
