EXECBIN  = boundedBuffer
SOURCES  = queue.c rwlock.c
OBJECTS  = queue.o rwlock.o
FORMATS  = $(SOURCES:%.c=%.fmt)
LIBRARIES = asgn2_helper_funcs.a

CC       = clang
FORMAT   = clang-format
CFLAGS   = -Wall -Wpedantic -Werror -Wextra

.PHONY: all clean format

all: $(OBJECTS)

queue.o: queue.c queue.h
	$(CC) $(CFLAGS) -c queue.c -o queue.o

rwlock.o: rwlock.c rwlock.h
	$(CC) $(CFLAGS) -c rwlock.c -o rwlock.o
clean:
	rm -f $(OBJECTS)

format: $(FORMATS)

%.fmt: %.c
	$(FORMAT) -i $<
	touch $@

