CC=gcc
CFLAGS=-Wall -Wextra -g
LDFLAGS=

SRCS=main.c execute.c parser.tab.c lex.yy.c
OBJS=$(SRCS:.c=.o)
TARGET=rickshell

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean