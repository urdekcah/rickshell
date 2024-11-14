CC := gcc
CFLAGS := -O3 -g0 -fvisibility=hidden -Wl,--strip-all -Wall -Wextra -Wconversion \
          -Wnull-dereference -Wshadow -Wlogical-op -Wuninitialized -fstrict-aliasing \
          -Werror -Wno-format-truncation -Iinclude

LDFLAGS := -static -Wl,--strip-all,--warn-common
LDLIBS := -lncurses -ltinfo -ldl

TARGET_DIR := target
LIB_DIR := $(TARGET_DIR)/lib
BUILTIN_LIB_DIR := lib/builtin
READLINE_DIR := lib/readline

BUILTIN_LIB := $(LIB_DIR)/libbuiltin.a
READLINE_LIB := $(LIB_DIR)/libreadline.a

TARGET := rickshell

READLINE_CFLAGS := -O3 -g0 -fvisibility=hidden -Wall \
                  -Wno-unused-variable -Wno-unused-function -Wno-sign-compare \
                  -Wno-parentheses \
                  -DHAVE_CONFIG_H -DNO_GETTIMEOFDAY

SRCS := $(wildcard *.c) $(wildcard $(BUILTIN_LIB_DIR)/*.c)
SRCS := $(filter-out lex.yy.c, $(SRCS))
OBJS := $(SRCS:.c=.o)
BUILTIN_OBJS := $(filter $(BUILTIN_LIB_DIR)/%.o,$(OBJS))
MAIN_OBJS := $(filter-out $(BUILTIN_OBJS),$(OBJS))

.PHONY: all clean build clean_target prepare_dirs clean_readline

all: build clean_target

prepare_dirs:
	@mkdir -p $(TARGET_DIR)
	@mkdir -p $(LIB_DIR)

clean_readline:
	@if [ -f $(READLINE_DIR)/Makefile ]; then \
		cd $(READLINE_DIR) && $(MAKE) clean; \
	fi

$(READLINE_LIB): prepare_dirs
	@echo "Building readline..."
	cd $(READLINE_DIR) && \
	./configure --disable-shared --enable-static \
		--disable-multibyte --disable-install-examples \
		--with-curses \
		ac_cv_func_gettimeofday=no && \
	$(MAKE) CFLAGS="$(READLINE_CFLAGS)" OBJECTS="readline.o vi_mode.o funmap.o keymaps.o parens.o search.o \
		rltty.o complete.o bind.o isearch.o display.o signals.o util.o kill.o \
		undo.o macro.o input.o callback.o terminal.o \
		text.o nls.o misc.o \
		history.o histexpand.o histfile.o histsearch.o shell.o \
		mbutil.o tilde.o colors.o parse-colors.o \
		xmalloc.o xfree.o compat.o"
	@cp $(READLINE_DIR)/libreadline.a $(READLINE_LIB)

$(BUILTIN_LIB): $(BUILTIN_OBJS) | prepare_dirs
	$(AR) $(ARFLAGS) $@ $^

build: $(READLINE_LIB) $(BUILTIN_LIB) $(TARGET)

$(TARGET): $(MAIN_OBJS) lex.yy.o $(BUILTIN_LIB) $(READLINE_LIB)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

lex.yy.o: lex.yy.c
	$(CC) $(CFLAGS) -Wno-null-dereference -Wno-error=null-dereference -c $< -o $@

clean_target: clean_readline
	@echo "Cleaning up object files..."
	@rm -f $(OBJS) lex.yy.o
	@rm -rf $(TARGET_DIR)

clean: clean_target
	rm -f $(TARGET)
	rm -rf $(TARGET_DIR)