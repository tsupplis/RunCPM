# pal Makefile for runcpm

PROG = runcpm

MFILE = Makefile

# Flags to pass to the compiler - add "-g" to include debug information
CFLAGS =-Wall -g -fPIC -I../lua
#CFLAGS =-Wall -O0 -fPIC 

LDFLAGS = -lncurses -L../lua -llua

# Objects to build
OBJS = ram.o cpu.o main.o cpm.o disk.o pal.o globals.o pal_posix.o luah.o

# Clean up program
RM = rm -f

#------------------------------------------------------------------------

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(OBJS) -o $(PROG) $(LDFLAGS)

pal_posix.o: pal_posix.c $(wildcard *.h) $(MFILE)
	$(CC) $(CFLAGS) -c pal_posix.c

luah.o: luah.c $(wildcard *.h) $(MFILE)
	$(CC) $(CFLAGS) -c luah.c

pal.o: pal.c $(wildcard *.h) $(MFILE)
	$(CC) $(CFLAGS) -c pal.c

disk.o: disk.c $(wildcard *.h) $(MFILE)
	$(CC) $(CFLAGS) -c disk.c

cpu.o: cpu.c $(wildcard *.h) $(MFILE)
	$(CC) $(CFLAGS) -c cpu.c

cpm.o: cpm.c $(wildcard *.h) $(MFILE)
	$(CC) $(CFLAGS) -c cpm.c

main.o: main.c $(wildcard *.h) $(MFILE)
	$(CC) $(CFLAGS) -c main.c

ram.o: ram.c $(wildcard *.h) $(MFILE)
	$(CC) $(CFLAGS) -c ram.c

rebuild: clean all

.PHONY: clean
clean:
	$(RM) *.o
	$(RM) $(PROG)
