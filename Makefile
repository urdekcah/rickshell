CC=gcc
CFLAGS=-O3 -Wall -Wextra -Wconversion -Wnull-dereference -Wshadow -Wlogical-op -Wuninitialized -fstrict-aliasing -Werror -Iinclude
LDFLAGS=-static -Wl,--warn-common
AR=ar
ARFLAGS=rcs

BUILTIN_SRCS=$(wildcard builtin/*.c)
BUILTIN_OBJS=$(BUILTIN_SRCS:.c=.o)
BUILTIN_LIB=lib/libbuiltin.a

SRCS=main.c array.c builtin.c error.c execute.c expr.c file.c job.c parser.tab.c lex.yy.c log.c map.c memory.c pipeline.c redirect.c strconv.c string.c variable.c
OBJS=$(SRCS:.c=.o)

TARGET=rickshell

all: $(BUILTIN_LIB) $(TARGET)

$(BUILTIN_LIB): $(BUILTIN_OBJS)
	@mkdir -p lib
	$(AR) $(ARFLAGS) $@ $^

$(TARGET): $(OBJS) $(BUILTIN_LIB)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(BUILTIN_LIB) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

lex.yy.o: lex.yy.c
	$(CC) $(CFLAGS) -Wno-null-dereference -Wno-error=null-dereference -c $< -o $@

clean:
	rm -f $(OBJS) $(BUILTIN_OBJS) $(TARGET) $(BUILTIN_LIB)
	rm -rf lib

.PHONY: all clean