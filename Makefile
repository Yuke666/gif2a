CC=gcc

CCFLAGS=-g -02 -Wall
CFLAGS = -Wall -g $(shell pkg-config --cflags ncurses)
NCURSESLIBS = $(shell pkg-config ncurses --static --libs)
LDLIBS = -lm --static $(NCURSESLIBS)

SOURCES=main.c

OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=gif2a

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(OBJECTS) $(LDLIBS) -o $@

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@