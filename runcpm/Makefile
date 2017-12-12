# pal Makefile for runcpm

# Flags to pass to the compiler - add "-g" to include debug information
#CFLAGS=-Wall -g -fPIC -I../lua -DEMULATOR_HAS_LUA
#LDFLAGS=-lncurses -L../lua -llua
#PROG_EXT=

CFLAGS=-Wall -O0 -fPIC 
LDFLAGS=-lncurses
PROG_EXT=

#CC=i586-pc-msdosdjgpp-gcc
#CFLAGS=-Wall -O0
#PROG_EXT=.exe

#CC=i586-mingw32-gcc -mconsole
#CFLAGS=-Wall -O0
#PROG_EXT=.exe

PROG = runcpm
MFILE = Makefile

# Objects to build
OBJS = ram.o cpu.o main.o cpm.o disk.o pal.o globals.o pal_posixish.o luah.o

# Clean up program
RM = rm -f

#------------------------------------------------------------------------

all: $(PROG)$(PROG_EXT)

$(PROG)$(PROG_EXT): $(OBJS)
	$(CC) $(OBJS) -o $(PROG)$(PROG_EXT) $(LDFLAGS)

pal_posixish.o: pal_posixish.c $(wildcard *.h) $(MFILE)
	$(CC) $(CFLAGS) -c pal_posixish.c

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
	$(RM) $(PROG) $(PROG)$(PROG_EXT)
