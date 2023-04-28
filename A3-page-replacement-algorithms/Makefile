SRCS = simpleloop.c matmul.c blocked.c my_prog
PROGS = simpleloop matmul blocked my_prog

all : $(PROGS)

$(PROGS) : % : %.c
	gcc -Wall -g -o $@ $<


traces: $(PROGS)
	./runit simpleloop
	./runit matmul 100
	./runit blocked 100 25
	./runit my_prog

.PHONY: clean
clean :
	rm -f simpleloop matmul blocked my_prog tr-*.ref *.marker *~
