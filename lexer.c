/*
 * lexer.c — Mode-based tokenizer that resolves quoting into word segments.
 *
 * The scanner walks the source once, left to right. Operators are matched
 * greedily (">>" before ">", "&&" before "&"). A word is assembled segment by
 * segment: as the scanner crosses a quote boundary it flushes the literal run
 * it was accumulating and records the new quoting context, so the expansion
 * pass can later tell '$x' (literal) from "$x" (expanded) and decide what to
 * field-split and glob.
 *
 * All state lives in the caller's Lexer; nothing here touches globals, so a
 * command substitution body can be tokenized by a second, independent lexer
 * while the outer one is paused.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "lexer.h"
#include "word.h"
#include "rstring.h"

/* ---- character classification (locale-independent, sign-safe) ---- */

static bool is_blank(char c) {
  return c == ' ' || c == '\t';
}

static bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

static bool is_name_start(char c) {
  return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_name_char(char c) {
  return is_name_start(c) || is_digit(c);
}

/* Special single-character parameters: $?, $$, $!, $#, $@, $*, $-, $0..$9. */
static bool is_special_param(char c) {
  return c == '?' || c == '$' || c == '!' || c == '#' ||
         c == '@' || c == '*' || c == '-' || is_digit(c);
}

/* Unquoted characters that terminate a word. */
static bool is_word_delim(char c) {
  return c == '\0' || c == ' ' || c == '\t' || c == '\n' ||
         c == '|'  || c == '&' || c == ';'  ||
         c == '('  || c == ')' || c == '<'  || c == '>';
}

/* ---- low-level cursor ---- */

static char at(const Lexer* lx, size_t off) {
  size_t i = lx->pos + off;
  return (i < lx->len) ? lx->src[i] : '\0';
}

static char cur(const Lexer* lx) {
  return at(lx, 0);
}

static void adv(Lexer* lx) {
  if (lx->pos < lx->len) lx->pos++;
}

static void set_error(Lexer* lx, const char* msg) {
  if (!lx->error) {
    lx->error = true;
    lx->errmsg = string__new(msg);
  }
}

/* Records an error caused by end of input inside an unterminated construct. The
 * incomplete flag lets an interactive caller keep reading more input instead of
 * reporting a syntax error. */
static void set_unterminated(Lexer* lx, const char* msg) {
  set_error(lx, msg);
  lx->incomplete = true;
}

/* ---- literal-run accumulator ----
 *
 * Adjacent literal characters of the same quoting context are coalesced into a
 * single segment to avoid one segment per character. A change in quoting (an
 * escaped character next to an unquoted one) flushes the pending run.
 */
typedef struct {
  StringBuilder sb;
  bool          active;
  bool          quoted;
} LitBuf;

static void litbuf_init(LitBuf* lb) {
  lb->active = false;
  lb->quoted = false;
}

static void litbuf_flush(Word* w, LitBuf* lb) {
  if (!lb->active) return;
  string text = string_builder__to_string(&lb->sb);
  string_builder__free(&lb->sb);
  lb->active = false;
  word_push(w, SEG_LITERAL, text, lb->quoted);
}

static void litbuf_push_char(Word* w, LitBuf* lb, char c, bool quoted) {
  if (lb->active && lb->quoted != quoted) litbuf_flush(w, lb);
  if (!lb->active) {
    lb->sb = string_builder__new();
    lb->quoted = quoted;
    lb->active = true;
  }
  string_builder__append_char(&lb->sb, c);
}

/* ---- expansion readers ----
 *
 * Each reader is entered with the cursor on the introducing character and
 * leaves it just past the construct it consumed. On a syntax error it records
 * the message on the lexer, releases any scratch buffer, and returns false; the
 * partially built Word is owned and freed by the top-level word reader.
 */

static bool read_dollar(Lexer* lx, Word* w, bool quoted);

/* Backtick command substitution: `...`. Backslash escapes ` \ $ inside. */
static bool read_backtick(Lexer* lx, Word* w, bool quoted) {
  adv(lx);  /* consume opening backtick */
  StringBuilder sb = string_builder__new();
  char c;
  while ((c = cur(lx)) != '\0' && c != '`') {
    if (c == '\\') {
      char n = at(lx, 1);
      if (n == '`' || n == '\\' || n == '$') {
        string_builder__append_char(&sb, n);
        adv(lx);
        adv(lx);
        continue;
      }
      string_builder__append_char(&sb, '\\');
      adv(lx);
      continue;
    }
    string_builder__append_char(&sb, c);
    adv(lx);
  }
  if (c == '\0') {
    string_builder__free(&sb);
    set_unterminated(lx, "unterminated `...` command substitution");
    return false;
  }
  adv(lx);  /* consume closing backtick */
  string text = string_builder__to_string(&sb);
  string_builder__free(&sb);
  word_push(w, SEG_COMMAND, text, quoted);
  return true;
}

/* ${...}: stored verbatim, including the leading "$" and braces, for reuse by
 * the parameter-expansion engine. Brace nesting is balanced. */
static bool read_braced_param(Lexer* lx, Word* w, bool quoted) {
  StringBuilder sb = string_builder__new();
  string_builder__append_char(&sb, '$');
  string_builder__append_char(&sb, '{');
  adv(lx);  /* consume '{' */
  int depth = 1;
  char c;
  while ((c = cur(lx)) != '\0') {
    if (c == '{') {
      depth++;
    } else if (c == '}') {
      depth--;
      string_builder__append_char(&sb, c);
      adv(lx);
      if (depth == 0) break;
      continue;
    }
    string_builder__append_char(&sb, c);
    adv(lx);
  }
  if (depth != 0) {
    string_builder__free(&sb);
    set_unterminated(lx, "unterminated ${...} expansion");
    return false;
  }
  string text = string_builder__to_string(&sb);
  string_builder__free(&sb);
  word_push(w, SEG_PARAM, text, quoted);
  return true;
}

/* "$((...))" and the "((...))" command share this reader: the inner expression
 * is stored without the "((" / "))" markers, with parentheses balanced. */
static bool read_arith(Lexer* lx, Word* w, bool quoted) {
  adv(lx);  /* consume first '(' */
  adv(lx);  /* consume second '(' */
  StringBuilder sb = string_builder__new();
  int depth = 0;
  for (;;) {
    char c = cur(lx);
    if (c == '\0') {
      string_builder__free(&sb);
      set_unterminated(lx, "unterminated arithmetic expression");
      return false;
    }
    if (c == '(') {
      depth++;
      string_builder__append_char(&sb, c);
      adv(lx);
      continue;
    }
    if (c == ')') {
      if (depth > 0) {
        depth--;
        string_builder__append_char(&sb, c);
        adv(lx);
        continue;
      }
      if (at(lx, 1) == ')') {
        adv(lx);
        adv(lx);
        break;
      }
      string_builder__free(&sb);
      set_error(lx, "malformed arithmetic expression");
      return false;
    }
    string_builder__append_char(&sb, c);
    adv(lx);
  }
  string text = string_builder__to_string(&sb);
  string_builder__free(&sb);
  word_push(w, SEG_ARITH, text, quoted);
  return true;
}

/* $(...): the inner source is stored without the "$(" / ")". Parentheses are
 * balanced, and single- and double-quoted spans inside are copied verbatim so a
 * ")" within a quoted argument does not close the substitution early. */
static bool read_cmdsub(Lexer* lx, Word* w, bool quoted) {
  adv(lx);  /* consume '(' */
  StringBuilder sb = string_builder__new();
  int depth = 1;
  for (;;) {
    char c = cur(lx);
    if (c == '\0') {
      string_builder__free(&sb);
      set_unterminated(lx, "unterminated $(...) command substitution");
      return false;
    }
    if (c == '\'') {
      string_builder__append_char(&sb, c);
      adv(lx);
      while ((c = cur(lx)) != '\0' && c != '\'') {
        string_builder__append_char(&sb, c);
        adv(lx);
      }
      if (c == '\0') continue;  /* loop top reports the unterminated error */
      string_builder__append_char(&sb, '\'');
      adv(lx);
      continue;
    }
    if (c == '"') {
      string_builder__append_char(&sb, c);
      adv(lx);
      while ((c = cur(lx)) != '\0' && c != '"') {
        if (c == '\\') {
          string_builder__append_char(&sb, c);
          adv(lx);
          if ((c = cur(lx)) != '\0') {
            string_builder__append_char(&sb, c);
            adv(lx);
          }
          continue;
        }
        string_builder__append_char(&sb, c);
        adv(lx);
      }
      if (c == '\0') continue;
      string_builder__append_char(&sb, '"');
      adv(lx);
      continue;
    }
    if (c == '\\') {
      string_builder__append_char(&sb, c);
      adv(lx);
      if ((c = cur(lx)) != '\0') {
        string_builder__append_char(&sb, c);
        adv(lx);
      }
      continue;
    }
    if (c == '(') {
      depth++;
      string_builder__append_char(&sb, c);
      adv(lx);
      continue;
    }
    if (c == ')') {
      depth--;
      if (depth == 0) {
        adv(lx);
        break;
      }
      string_builder__append_char(&sb, c);
      adv(lx);
      continue;
    }
    string_builder__append_char(&sb, c);
    adv(lx);
  }
  string text = string_builder__to_string(&sb);
  string_builder__free(&sb);
  word_push(w, SEG_COMMAND, text, quoted);
  return true;
}

static bool read_dollar(Lexer* lx, Word* w, bool quoted) {
  adv(lx);  /* consume '$' */
  char c = cur(lx);

  if (c == '{') return read_braced_param(lx, w, quoted);
  if (c == '(') {
    if (at(lx, 1) == '(') return read_arith(lx, w, quoted);
    return read_cmdsub(lx, w, quoted);
  }

  if (is_name_start(c)) {
    StringBuilder sb = string_builder__new();
    string_builder__append_char(&sb, '$');
    while (is_name_char(cur(lx))) {
      string_builder__append_char(&sb, cur(lx));
      adv(lx);
    }
    /* A trailing [index] is kept with the name so array references like
     * $arr[0] reach the parameter engine intact. */
    if (cur(lx) == '[') {
      string_builder__append_char(&sb, '[');
      adv(lx);
      int depth = 1;
      while ((c = cur(lx)) != '\0') {
        if (c == '[') depth++;
        else if (c == ']') depth--;
        string_builder__append_char(&sb, c);
        adv(lx);
        if (depth == 0) break;
      }
    }
    string text = string_builder__to_string(&sb);
    string_builder__free(&sb);
    word_push(w, SEG_PARAM, text, quoted);
    return true;
  }

  if (is_special_param(c)) {
    StringBuilder sb = string_builder__new();
    string_builder__append_char(&sb, '$');
    string_builder__append_char(&sb, c);
    adv(lx);
    string text = string_builder__to_string(&sb);
    string_builder__free(&sb);
    word_push(w, SEG_PARAM, text, quoted);
    return true;
  }

  /* A "$" not introducing an expansion is an ordinary literal dollar sign. */
  word_push(w, SEG_LITERAL, string__new("$"), quoted);
  return true;
}

/* '...': everything up to the next single quote is literal, with no escapes. */
static bool read_squote(Lexer* lx, Word* w) {
  adv(lx);  /* consume opening quote */
  StringBuilder sb = string_builder__new();
  char c;
  while ((c = cur(lx)) != '\0' && c != '\'') {
    string_builder__append_char(&sb, c);
    adv(lx);
  }
  if (c == '\0') {
    string_builder__free(&sb);
    set_unterminated(lx, "unterminated ' quote");
    return false;
  }
  adv(lx);  /* consume closing quote */
  string text = string_builder__to_string(&sb);
  string_builder__free(&sb);
  word_push(w, SEG_LITERAL, text, true);  /* keeps the empty-string case as a field */
  return true;
}

/* "...": literal, but $, ` and the escapes \$ \` \" \\ \<newline> are active. */
static bool read_dquote(Lexer* lx, Word* w) {
  adv(lx);  /* consume opening quote */
  LitBuf lb;
  litbuf_init(&lb);
  bool any = false;
  char c;
  while ((c = cur(lx)) != '\0' && c != '"') {
    if (c == '\\') {
      char n = at(lx, 1);
      if (n == '$' || n == '`' || n == '"' || n == '\\') {
        litbuf_push_char(w, &lb, n, true);
        any = true;
        adv(lx);
        adv(lx);
        continue;
      }
      if (n == '\n') {  /* line continuation: both characters vanish */
        adv(lx);
        adv(lx);
        continue;
      }
      litbuf_push_char(w, &lb, '\\', true);
      any = true;
      adv(lx);
      continue;
    }
    if (c == '$') {
      litbuf_flush(w, &lb);
      if (!read_dollar(lx, w, true)) return false;
      any = true;
      continue;
    }
    if (c == '`') {
      litbuf_flush(w, &lb);
      if (!read_backtick(lx, w, true)) return false;
      any = true;
      continue;
    }
    litbuf_push_char(w, &lb, c, true);
    any = true;
    adv(lx);
  }
  if (c == '\0') {
    litbuf_flush(w, &lb);
    set_unterminated(lx, "unterminated \" quote");
    return false;
  }
  litbuf_flush(w, &lb);
  adv(lx);  /* consume closing quote */
  if (!any) word_push(w, SEG_LITERAL, string__new(""), true);
  return true;
}

static bool read_word(Lexer* lx, Token* out) {
  Word* w = word_new();
  LitBuf lb;
  litbuf_init(&lb);

  for (;;) {
    char c = cur(lx);
    if (is_word_delim(c)) break;

    if (c == '\'') {
      litbuf_flush(w, &lb);
      if (!read_squote(lx, w)) { word_free(w); return false; }
      continue;
    }
    if (c == '"') {
      litbuf_flush(w, &lb);
      if (!read_dquote(lx, w)) { word_free(w); return false; }
      continue;
    }
    if (c == '`') {
      litbuf_flush(w, &lb);
      if (!read_backtick(lx, w, false)) { word_free(w); return false; }
      continue;
    }
    if (c == '$') {
      litbuf_flush(w, &lb);
      if (!read_dollar(lx, w, false)) { word_free(w); return false; }
      continue;
    }
    if (c == '\\') {
      char n = at(lx, 1);
      if (n == '\0') {  /* trailing backslash is a literal backslash */
        litbuf_push_char(w, &lb, '\\', true);
        adv(lx);
        continue;
      }
      if (n == '\n') {  /* line continuation */
        adv(lx);
        adv(lx);
        continue;
      }
      litbuf_push_char(w, &lb, n, true);  /* escaped char: literal, no split/glob */
      adv(lx);
      adv(lx);
      continue;
    }

    litbuf_push_char(w, &lb, c, false);
    adv(lx);
  }

  litbuf_flush(w, &lb);
  out->type = TOK_WORD;
  out->word = w;
  return true;
}

/* "<", and the operators that begin with it. */
static void read_less(Lexer* lx, Token* out) {
  adv(lx);  /* consume '<' */
  char c = cur(lx);
  if (c == '<') {
    adv(lx);
    if (cur(lx) == '-') { adv(lx); out->type = TOK_DLESSDASH; }
    else out->type = TOK_DLESS;
    return;
  }
  if (c == '&') { adv(lx); out->type = TOK_LESSAND; return; }
  if (c == '>') { adv(lx); out->type = TOK_LESSGREAT; return; }
  out->type = TOK_LESS;
}

/* ">", and the operators that begin with it. */
static void read_great(Lexer* lx, Token* out) {
  adv(lx);  /* consume '>' */
  char c = cur(lx);
  if (c == '>') {
    adv(lx);
    if (cur(lx) == '&') { adv(lx); out->type = TOK_DGREATAND; }
    else out->type = TOK_DGREAT;
    return;
  }
  if (c == '&') { adv(lx); out->type = TOK_GREATAND; return; }
  if (c == '|') { adv(lx); out->type = TOK_CLOBBER; return; }
  out->type = TOK_GREAT;
}

void lexer_init(Lexer* lx, const char* src) {
  lx->src = src;
  lx->len = strlen(src);
  lx->pos = 0;
  lx->error = false;
  lx->incomplete = false;
  lx->errmsg = _SLIT0;
}

void lexer_dispose(Lexer* lx) {
  if (lx->error) string__free(lx->errmsg);
  lx->errmsg = _SLIT0;
  lx->error = false;
}

bool lexer_next(Lexer* lx, Token* out) {
  out->type = TOK_EOF;
  out->word = NULL;
  out->io_number = 0;
  if (lx->error) return false;

  /* Skip inter-token blanks, line continuations, and comments. A newline is a
   * token, so it is not skipped here. */
  for (;;) {
    char c = cur(lx);
    if (is_blank(c)) { adv(lx); continue; }
    if (c == '\\' && at(lx, 1) == '\n') { adv(lx); adv(lx); continue; }
    if (c == '#') {
      while ((c = cur(lx)) != '\0' && c != '\n') adv(lx);
      continue;
    }
    break;
  }

  char c = cur(lx);
  if (c == '\0') { out->type = TOK_EOF; return true; }
  if (c == '\n') { adv(lx); out->type = TOK_NEWLINE; return true; }

  switch (c) {
    case '|':
      adv(lx);
      if (cur(lx) == '|') { adv(lx); out->type = TOK_OR_IF; }
      else out->type = TOK_PIPE;
      return true;
    case '&':
      adv(lx);
      if (cur(lx) == '&') { adv(lx); out->type = TOK_AND_IF; }
      else out->type = TOK_AMP;
      return true;
    case ';':
      adv(lx);
      if (cur(lx) == ';') {
        adv(lx);
        if (cur(lx) == '&') { adv(lx); out->type = TOK_DSEMI_AMP; }
        else out->type = TOK_DSEMI;
      } else if (cur(lx) == '&') {
        adv(lx);
        out->type = TOK_SEMI_AMP;
      } else {
        out->type = TOK_SEMI;
      }
      return true;
    case '(':
      /* "((" introduces the arithmetic command; a nested subshell must be
       * written "( (" with a space, matching the usual shell disambiguation. */
      if (at(lx, 1) == '(') {
        Word* w = word_new();
        if (!read_arith(lx, w, false)) { word_free(w); return false; }
        out->type = TOK_ARITH;
        out->word = w;
        return true;
      }
      adv(lx);
      out->type = TOK_LPAREN;
      return true;
    case ')':
      adv(lx);
      out->type = TOK_RPAREN;
      return true;
    case '<':
      read_less(lx, out);
      return true;
    case '>':
      read_great(lx, out);
      return true;
    default:
      break;
  }

  /* A digit run is an IO_NUMBER only when it abuts a redirection operator. */
  if (is_digit(c)) {
    size_t save = lx->pos;
    while (is_digit(cur(lx))) adv(lx);
    char nxt = cur(lx);
    size_t ndigits = lx->pos - save;
    if ((nxt == '<' || nxt == '>') && ndigits <= 9) {
      char buf[10];
      memcpy(buf, lx->src + save, ndigits);
      buf[ndigits] = '\0';
      out->io_number = atoi(buf);
      out->type = TOK_IO_NUMBER;
      return true;
    }
    lx->pos = save;  /* re-scan the digits as the start of a word */
  }

  return read_word(lx, out);
}

void token_free(Token* t) {
  if (t->word != NULL) word_free(t->word);
  t->type = TOK_EOF;
  t->word = NULL;
  t->io_number = 0;
}
