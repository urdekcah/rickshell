CC=gcc
CFLAGS=-Wall -Wextra
LDFLAGS=

SRCS=main.c error.c execute.c expr.c file.c job.c parser.tab.c lex.yy.c log.c memory.c pipeline.c redirect.c
OBJS=$(SRCS:.c=.o)
TARGET=rickshell

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -g

%.o: %.c
	$(CC) $(CFLAGS) -c $< -g

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean