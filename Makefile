
CC = gcc
CFLAGS = -Wall
LDFLAGS =

OBJDIR = obj
# IDIR = include
# LDIR = src

LAV_SOURCES = src/Lavagna.c src/common.c
USR_SOURCES = src/Utente.c src/common.c

LAV_OBJ = $(LAV_SOURCES:src/%.c=$(OBJDIR)/%.o)
USR_OBJ = $(USR_SOURCES:src/%.c=$(OBJDIR)/%.o)

LAV_EXEC = lavagna
USR_EXEC = utente

all: $(LAV_EXEC) $(USR_EXEC)

$(LAV_EXEC): $(LAV_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(USR_EXEC): $(USR_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(OBJDIR)/%.o: src/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(LAV_OBJ) $(USR_OBJ) $(LAV_EXEC) $(USR_EXEC)

.PHONY: clean