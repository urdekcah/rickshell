CC := gcc
CFLAGS := -O3 -g0 -fvisibility=hidden -Wl,--strip-all -Wall -Wextra -Wconversion -Wnull-dereference -Wshadow -Wlogical-op -Wuninitialized -fstrict-aliasing -Werror -Iinclude
LDFLAGS := -static -Wl,--strip-all,--warn-common,-lreadline
AR := ar
ARFLAGS := rcs

TARGET := rickshell
BUILTIN_DIR := builtin
LIB_DIR := lib
BUILTIN_LIB := $(LIB_DIR)/libbuiltin.a

SRCS := $(wildcard *.c) $(wildcard $(BUILTIN_DIR)/*.c)
SRCS := $(filter-out lex.yy.c, $(SRCS))
OBJS := $(SRCS:.c=.o)
BUILTIN_OBJS := $(filter $(BUILTIN_DIR)/%.o,$(OBJS))
MAIN_OBJS := $(filter-out $(BUILTIN_OBJS),$(OBJS))

.PHONY: all clean build ochistka

all: build ochistka

build: $(BUILTIN_LIB) $(TARGET)

$(BUILTIN_LIB): $(BUILTIN_OBJS)
	@mkdir -p $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

$(TARGET): $(MAIN_OBJS) lex.yy.o $(BUILTIN_LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

lex.yy.o: lex.yy.c
	$(CC) $(CFLAGS) -Wno-null-dereference -Wno-error=null-dereference -c $< -o $@

ochistka:
	@echo "Cleaning up object files..."
	@rm -f $(OBJS) lex.yy.o $(BUILTIN_LIB)
	@rm -rf $(LIB_DIR)

clean:
	rm -f $(OBJS) lex.yy.o $(TARGET) $(BUILTIN_LIB)
	rm -rf $(LIB_DIR)