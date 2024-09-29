CFLAGS := -g -Wall -Werror -D_GNU_SOURCE

TARGETS := test_basic

# Make sure that 'all' is the first target
all: depend $(TARGETS)

clean:
	rm -rf core *.o $(TARGETS)

realclean: clean
	rm -rf *~ *.bak .depend *.log *.out

tags:
	etags *.c *.h

malloc.o: malloc.cpp
	g++ $(CFLAGS) -c -o malloc.o malloc.cpp

OBJS := test_basic.o thread.o malloc.o

$(TARGETS): $(OBJS)
	g++ $(CFLAGS) -o $@ $(OBJS)

depend:
	$(CC) -MM *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif
