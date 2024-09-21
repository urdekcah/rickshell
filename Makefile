CC=gcc
CFLAGS=-Wall -Wextra -g
LDFLAGS=

SRCS=main.c error.c execute.c expr.c parser.tab.c lex.yy.c memory.c pipeline.c redirect.c
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