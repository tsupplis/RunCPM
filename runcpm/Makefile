ifeq ($(PLAT),)
PLAT=none
endif

PLATS=linux macosx mingw djgpp

#DEBUG=yes
LUA=yes

PROG_EXT=

STRIP=strip
CFLAGS=-Wall
LDFLAGS=

ifeq ($(PLAT),macosx)
LDFLAGS=-lncurses
endif

ifeq ($(PLAT),linux)
CFLAGS+=-fPIC
LDFLAGS+=-lncurses
endif

ifeq ($(PLAT),djgpp)
LUA=false
CC=i586-pc-msdosdjgpp-gcc
STRIP=i586-pc-msdosdjgpp-strip
CFLAGS=-Wall
PROG_EXT=.exe
endif

ifeq ($(PLAT),mingw)
LUA=false
CC=i586-mingw32-gcc -mconsole
STRIP=i586-mingw32-strip
PROG_EXT=.exe
endif

ifeq ($(DEBUG),yes)
CFLAGS+=-g
else
CFLAGS+=-O0
endif

ifeq ($(LUA),yes)
CFLAGS+= -I../lua -DEMULATOR_HAS_LUA
LDFLAGS+=-L../lua -llua -ldl -lm
endif

PROG = runcpm
MFILE = Makefile

# Objects to build
OBJS = ram.o cpu.o main.o cpm.o disk.o pal.o globals.o pal_posixish.o luah.o \
 ccp.o ccp_emulated.o

# Clean up program
RM = rm -f

#------------------------------------------------------------------------

all: $(PLAT)

macosx:
	make prog PLAT=$@

linux:
	make prog PLAT=$@

djgpp:
	make prog PLAT=$@

mingw:
	make prog PLAT=$@

none:
	@echo "Please do 'make PLATFORM' where PLATFORM is one of these:"
	@echo "   $(PLATS)"

prog: $(PROG)$(PROG_EXT)

$(PROG)$(PROG_EXT): $(OBJS)
	$(CC) $(OBJS) -o $(PROG)$(PROG_EXT) $(LDFLAGS)
	$(STRIP) $(PROG)$(PROG_EXT)

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

ccp.o: ccp.c $(wildcard *.h) $(MFILE)
	$(CC) $(CFLAGS) -c ccp.c

main.o: main.c $(wildcard *.h) $(MFILE)
	$(CC) $(CFLAGS) -c main.c

ram.o: ram.c $(wildcard *.h) $(MFILE)
	$(CC) $(CFLAGS) -c ram.c

globals.o: globals.c $(wildcard *.h) $(MFILE)
	$(CC) $(CFLAGS) -c globals.c

.PHONY: clean
clean:
	$(RM) *.o
	$(RM) $(PROG) $(PROG)$(PROG_EXT)
