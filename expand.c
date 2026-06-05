/*
 * expand.c — POSIX word-expansion pass.
 *
 * A word is expanded into a flat byte buffer alongside two parallel bit tracks:
 * "splittable" marks bytes produced by an unquoted expansion (eligible for
 * field splitting) and "globbable" marks bytes that are unquoted (eligible to
 * act as pattern metacharacters). Carrying this per byte is what lets the same
 * buffer hold quoted and unquoted regions and still split and glob only the
 * parts that should be: x="a b"; echo $x splits, echo "$x" does not.
 *
 * Stage order matches the standard: tilde, parameter, command substitution and
 * arithmetic (as the buffer is built), then field splitting, then pathname
 * expansion. Quote removal needs no work here because the lexer never stored
 * quote characters in the first place.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pwd.h>
#include <glob.h>
#include <fnmatch.h>
#include <regex.h>
#include "expand.h"
#include "word.h"
#include "rstring.h"
#include "array.h"
#include "variable.h"
#include "memory.h"
#include "execute.h"

/* ---- expansion buffer: bytes plus per-byte split/glob eligibility ---- */

typedef struct {
  char*  buf;
  bool*  splittable;
  bool*  globbable;
  size_t len;
  size_t cap;
} EBuf;

static void ebuf_init(EBuf* e) {
  e->buf = NULL;
  e->splittable = NULL;
  e->globbable = NULL;
  e->len = 0;
  e->cap = 0;
}

static void ebuf_free(EBuf* e) {
  rfree(e->buf);
  rfree(e->splittable);
  rfree(e->globbable);
}

static void ebuf_reserve(EBuf* e, size_t extra) {
  if (e->len + extra + 1 <= e->cap) return;
  size_t want = e->len + extra + 1;
  size_t cap = (e->cap == 0) ? 16 : e->cap;
  while (cap < want) cap *= 2;
  e->buf = rrealloc(e->buf, cap);
  e->splittable = rrealloc(e->splittable, cap * sizeof(bool));
  e->globbable = rrealloc(e->globbable, cap * sizeof(bool));
  e->cap = cap;
}

static void ebuf_append(EBuf* e, const char* s, size_t n, bool splittable, bool globbable) {
  if (n == 0) return;
  ebuf_reserve(e, n);
  for (size_t i = 0; i < n; i++) {
    e->buf[e->len + i] = s[i];
    e->splittable[e->len + i] = splittable;
    e->globbable[e->len + i] = globbable;
  }
  e->len += n;
  e->buf[e->len] = '\0';
}

/* ---- arithmetic evaluation ($(( )), (( )), for ((;;))) ----
 *
 * A small precedence-climbing evaluator over signed 64-bit integers. Bare
 * identifiers resolve to variables; an unset or non-numeric variable reads as 0.
 * Assignment (= and the compound forms), pre/post-increment and -decrement, and
 * the comma operator write results back through set_variable(). Any syntax error
 * or division by zero sets ok = false and the result is 0.
 */

typedef struct {
  const char*    s;
  size_t         n;
  size_t         i;
  VariableTable* table;
  bool           ok;
} Arith;

static long long arith_expr(Arith* a);

static void arith_ws(Arith* a) {
  while (a->i < a->n && (a->s[a->i] == ' ' || a->s[a->i] == '\t' || a->s[a->i] == '\n'))
    a->i++;
}

static char arith_peek(const Arith* a) {
  return (a->i < a->n) ? a->s[a->i] : '\0';
}

static long long arith_lookup(Arith* a, const char* name, size_t len) {
  string key = string__new(name);  /* name is a NUL-terminated scratch copy */
  Variable* v = get_variable(a->table, key);
  string__free(key);
  (void)len;
  if (v == NULL) return 0;
  char* end = NULL;
  long long val = strtoll(v->str.str, &end, 0);
  if (end == v->str.str) return 0;
  return val;
}

/* Writes @p val to the named variable as a decimal string. */
static void arith_set(Arith* a, const char* name, long long val) {
  string key = string__new(name);
  char vbuf[32];
  int wn = snprintf(vbuf, sizeof(vbuf), "%lld", val);
  string vs = string__new(wn > 0 ? vbuf : "0");
  set_variable(a->table, key, vs, VAR_STRING, false);
  string__free(key);
  string__free(vs);
}

/* Reads an identifier at the cursor into @p buf and advances past it. Returns
 * the name length, or 0 when the cursor is not on an identifier (cursor kept).
 * A name longer than the buffer sets ok = false and returns 0. */
static size_t arith_read_name(Arith* a, char* buf, size_t bufsz) {
  arith_ws(a);
  size_t start = a->i;
  if (!(a->i < a->n && (a->s[a->i] == '_' || isalpha((unsigned char)a->s[a->i]))))
    return 0;
  while (a->i < a->n) {
    char d = a->s[a->i];
    if (d == '_' || isalnum((unsigned char)d)) a->i++;
    else break;
  }
  size_t len = a->i - start;
  if (len >= bufsz) { a->ok = false; return 0; }
  memcpy(buf, a->s + start, len);
  buf[len] = '\0';
  return len;
}

/* Recognizes an assignment operator at the cursor without consuming it. Sets
 * @p kind to a one-character tag ('=', '+', '-', '*', '/', '%', '&', '^', '|',
 * 'L' for "<<=", 'R' for ">>=") and returns the operator length in bytes, or 0
 * when the cursor is not on an assignment operator. */
static int arith_assign_op(const Arith* a, char* kind) {
  char c0 = (a->i < a->n) ? a->s[a->i] : '\0';
  char c1 = (a->i + 1 < a->n) ? a->s[a->i + 1] : '\0';
  char c2 = (a->i + 2 < a->n) ? a->s[a->i + 2] : '\0';
  if (c0 == '<' && c1 == '<' && c2 == '=') { *kind = 'L'; return 3; }
  if (c0 == '>' && c1 == '>' && c2 == '=') { *kind = 'R'; return 3; }
  if (c1 == '=' && (c0 == '+' || c0 == '-' || c0 == '*' || c0 == '/' ||
                    c0 == '%' || c0 == '&' || c0 == '^' || c0 == '|')) {
    *kind = c0;
    return 2;
  }
  if (c0 == '=' && c1 != '=') { *kind = '='; return 1; }
  return 0;
}

static long long arith_primary(Arith* a) {
  arith_ws(a);
  char c = arith_peek(a);

  if (c == '(') {
    a->i++;
    long long v = arith_expr(a);
    arith_ws(a);
    if (arith_peek(a) == ')') a->i++;
    else a->ok = false;
    return v;
  }

  if ((c >= '0' && c <= '9')) {
    char* end = NULL;
    long long v = strtoll(a->s + a->i, &end, 0);
    size_t consumed = (size_t)(end - (a->s + a->i));
    if (consumed == 0) { a->ok = false; return 0; }
    a->i += consumed;
    return v;
  }

  if (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
    size_t start = a->i;
    while (a->i < a->n) {
      char d = a->s[a->i];
      if (d == '_' || (d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') || (d >= '0' && d <= '9'))
        a->i++;
      else
        break;
    }
    size_t len = a->i - start;
    char scratch[256];
    if (len >= sizeof(scratch)) { a->ok = false; return 0; }
    memcpy(scratch, a->s + start, len);
    scratch[len] = '\0';
    return arith_lookup(a, scratch, len);
  }

  a->ok = false;
  return 0;
}

/* Postfix "++"/"--". Reads the lvalue itself, restoring the cursor when no
 * postfix operator follows so the bare identifier still reaches arith_primary. */
static long long arith_postfix(Arith* a) {
  arith_ws(a);
  size_t save = a->i;
  char name[256];
  size_t nlen = arith_read_name(a, name, sizeof(name));
  if (nlen > 0) {
    arith_ws(a);
    char c = arith_peek(a);
    if ((c == '+' || c == '-') && a->i + 1 < a->n && a->s[a->i + 1] == c) {
      a->i += 2;
      long long old = arith_lookup(a, name, nlen);
      arith_set(a, name, (c == '+') ? old + 1 : old - 1);
      return old;
    }
    a->i = save;  /* a plain identifier; let arith_primary read its value */
  }
  return arith_primary(a);
}

static long long arith_unary(Arith* a) {
  arith_ws(a);
  char c = arith_peek(a);
  if ((c == '+' || c == '-') && a->i + 1 < a->n && a->s[a->i + 1] == c) {
    a->i += 2;
    char name[256];
    size_t nlen = arith_read_name(a, name, sizeof(name));
    if (nlen == 0) { a->ok = false; return 0; }
    long long v = (c == '+') ? arith_lookup(a, name, nlen) + 1
                             : arith_lookup(a, name, nlen) - 1;
    arith_set(a, name, v);
    return v;
  }
  if (c == '-') { a->i++; return -arith_unary(a); }
  if (c == '+') { a->i++; return arith_unary(a); }
  if (c == '!') { a->i++; return !arith_unary(a); }
  if (c == '~') { a->i++; return ~arith_unary(a); }
  return arith_postfix(a);
}

static long long arith_mul(Arith* a) {
  long long v = arith_unary(a);
  for (;;) {
    arith_ws(a);
    char c = arith_peek(a);
    if (c == '*') { a->i++; v = v * arith_unary(a); }
    else if (c == '/') {
      a->i++;
      long long r = arith_unary(a);
      if (r == 0) { a->ok = false; return 0; }
      v = v / r;
    } else if (c == '%') {
      a->i++;
      long long r = arith_unary(a);
      if (r == 0) { a->ok = false; return 0; }
      v = v % r;
    } else break;
  }
  return v;
}

static long long arith_add(Arith* a) {
  long long v = arith_mul(a);
  for (;;) {
    arith_ws(a);
    char c = arith_peek(a);
    if (c == '+') { a->i++; v = v + arith_mul(a); }
    else if (c == '-') { a->i++; v = v - arith_mul(a); }
    else break;
  }
  return v;
}

static long long arith_shift(Arith* a) {
  long long v = arith_add(a);
  for (;;) {
    arith_ws(a);
    if (arith_peek(a) == '<' && a->i + 1 < a->n && a->s[a->i + 1] == '<') {
      a->i += 2;
      v = v << arith_add(a);
    } else if (arith_peek(a) == '>' && a->i + 1 < a->n && a->s[a->i + 1] == '>') {
      a->i += 2;
      v = v >> arith_add(a);
    } else break;
  }
  return v;
}

static long long arith_rel(Arith* a) {
  long long v = arith_shift(a);
  for (;;) {
    arith_ws(a);
    char c = arith_peek(a);
    if (c == '<' && a->i + 1 < a->n && a->s[a->i + 1] == '=') { a->i += 2; v = (v <= arith_shift(a)); }
    else if (c == '>' && a->i + 1 < a->n && a->s[a->i + 1] == '=') { a->i += 2; v = (v >= arith_shift(a)); }
    else if (c == '<') { a->i++; v = (v < arith_shift(a)); }
    else if (c == '>') { a->i++; v = (v > arith_shift(a)); }
    else break;
  }
  return v;
}

static long long arith_eq(Arith* a) {
  long long v = arith_rel(a);
  for (;;) {
    arith_ws(a);
    char c = arith_peek(a);
    if (c == '=' && a->i + 1 < a->n && a->s[a->i + 1] == '=') { a->i += 2; v = (v == arith_rel(a)); }
    else if (c == '!' && a->i + 1 < a->n && a->s[a->i + 1] == '=') { a->i += 2; v = (v != arith_rel(a)); }
    else break;
  }
  return v;
}

static long long arith_band(Arith* a) {
  long long v = arith_eq(a);
  for (;;) {
    arith_ws(a);
    if (arith_peek(a) == '&' && !(a->i + 1 < a->n && a->s[a->i + 1] == '&')) { a->i++; v = v & arith_eq(a); }
    else break;
  }
  return v;
}

static long long arith_bxor(Arith* a) {
  long long v = arith_band(a);
  for (;;) {
    arith_ws(a);
    if (arith_peek(a) == '^') { a->i++; v = v ^ arith_band(a); }
    else break;
  }
  return v;
}

static long long arith_bor(Arith* a) {
  long long v = arith_bxor(a);
  for (;;) {
    arith_ws(a);
    if (arith_peek(a) == '|' && !(a->i + 1 < a->n && a->s[a->i + 1] == '|')) { a->i++; v = v | arith_bxor(a); }
    else break;
  }
  return v;
}

static long long arith_land(Arith* a) {
  long long v = arith_bor(a);
  for (;;) {
    arith_ws(a);
    if (arith_peek(a) == '&' && a->i + 1 < a->n && a->s[a->i + 1] == '&') { a->i += 2; long long r = arith_bor(a); v = (v && r); }
    else break;
  }
  return v;
}

static long long arith_lor(Arith* a) {
  long long v = arith_land(a);
  for (;;) {
    arith_ws(a);
    if (arith_peek(a) == '|' && a->i + 1 < a->n && a->s[a->i + 1] == '|') { a->i += 2; long long r = arith_land(a); v = (v || r); }
    else break;
  }
  return v;
}

/* Assignment, including the compound forms. Right-associative; the left side
 * must be a bare identifier. A leading identifier that is not followed by an
 * assignment operator is rewound and parsed as an ordinary expression. */
static long long arith_assign(Arith* a) {
  arith_ws(a);
  size_t save = a->i;
  char name[256];
  size_t nlen = arith_read_name(a, name, sizeof(name));
  if (nlen > 0) {
    arith_ws(a);
    char kind = 0;
    int oplen = arith_assign_op(a, &kind);
    if (oplen > 0) {
      a->i += (size_t)oplen;
      long long rhs = arith_assign(a);
      long long cur = arith_lookup(a, name, nlen);
      long long nv = rhs;
      switch (kind) {
        case '=': nv = rhs; break;
        case '+': nv = cur + rhs; break;
        case '-': nv = cur - rhs; break;
        case '*': nv = cur * rhs; break;
        case '/': if (rhs == 0) { a->ok = false; return 0; } nv = cur / rhs; break;
        case '%': if (rhs == 0) { a->ok = false; return 0; } nv = cur % rhs; break;
        case '&': nv = cur & rhs; break;
        case '^': nv = cur ^ rhs; break;
        case '|': nv = cur | rhs; break;
        case 'L': nv = cur << rhs; break;
        case 'R': nv = cur >> rhs; break;
        default: break;
      }
      arith_set(a, name, nv);
      return nv;
    }
  }
  a->i = save;
  return arith_lor(a);
}

static long long arith_comma(Arith* a) {
  long long v = arith_assign(a);
  for (;;) {
    arith_ws(a);
    if (arith_peek(a) == ',') { a->i++; v = arith_assign(a); }
    else break;
  }
  return v;
}

static long long arith_expr(Arith* a) {
  return arith_comma(a);
}

/* Evaluates an arithmetic source string (after parameter expansion). */
static long long arith_eval(VariableTable* table, const string src, bool* ok) {
  string expanded = expand_variables(table, src);
  Arith a;
  a.s = expanded.str;
  a.n = expanded.len;
  a.i = 0;
  a.table = table;
  a.ok = true;
  long long v = arith_expr(&a);
  arith_ws(&a);
  if (a.i != a.n) a.ok = false;
  *ok = a.ok;
  string__free(expanded);
  return v;
}

/* ---- tilde ---- */

/* Resolves a "~" or "~user" prefix to a home directory.
 * Returns an owned C string, or NULL when the user is unknown. */
static char* resolve_tilde(const char* user, size_t len) {
  if (len == 0) {
    const char* home = getenv("HOME");
    if (home != NULL) return rstrdup(home);
    struct passwd* pw = getpwuid(getuid());
    return (pw != NULL) ? rstrdup(pw->pw_dir) : NULL;
  }
  char name[256];
  if (len >= sizeof(name)) return NULL;
  memcpy(name, user, len);
  name[len] = '\0';
  struct passwd* pw = getpwnam(name);
  return (pw != NULL) ? rstrdup(pw->pw_dir) : NULL;
}

/* ---- segment emission ---- */

static void emit_segment(EBuf* e, VariableTable* table, const WordSeg* seg, bool is_first) {
  bool active = !seg->quoted;  /* unquoted results are split and globbed */

  switch (seg->kind) {
    case SEG_LITERAL: {
      const char* t = seg->text.str;
      size_t n = seg->text.len;
      /* Leading "~" on the first unquoted literal is a tilde prefix. */
      if (is_first && !seg->quoted && n > 0 && t[0] == '~') {
        size_t k = 1;
        while (k < n && t[k] != '/') k++;
        char* home = resolve_tilde(t + 1, k - 1);
        if (home != NULL) {
          ebuf_append(e, home, strlen(home), false, false);
          rfree(home);
          ebuf_append(e, t + k, n - k, false, true);
          return;
        }
      }
      ebuf_append(e, t, n, false, active);
      return;
    }
    case SEG_PARAM: {
      string v = expand_variables(table, seg->text);
      ebuf_append(e, v.str, v.len, active, active);
      string__free(v);
      return;
    }
    case SEG_ARITH: {
      bool ok = true;
      long long v = arith_eval(table, seg->text, &ok);
      char num[32];
      int wn = snprintf(num, sizeof(num), "%lld", ok ? v : 0);
      if (wn > 0) ebuf_append(e, num, (size_t)wn, active, active);
      return;
    }
    case SEG_COMMAND: {
      string out = command_substitution(seg->text);
      size_t n = out.len;
      while (n > 0 && out.str[n - 1] == '\n') n--;  /* trailing newlines are stripped */
      ebuf_append(e, out.str, n, active, active);
      string__free(out);
      return;
    }
  }
}

/* ---- field splitting ---- */

typedef struct {
  size_t start;
  size_t end;
} Range;

typedef struct {
  Range* data;
  size_t n;
  size_t cap;
} RangeList;

static void ranges_push(RangeList* rl, size_t start, size_t end) {
  if (rl->n == rl->cap) {
    rl->cap = (rl->cap == 0) ? 8 : rl->cap * 2;
    rl->data = rrealloc(rl->data, rl->cap * sizeof(Range));
  }
  rl->data[rl->n].start = start;
  rl->data[rl->n].end = end;
  rl->n++;
}

typedef struct {
  string ifs;       /* owned copy of the active IFS, or the default */
  bool   is_default;
  bool   no_split;  /* IFS explicitly set to the empty string */
} Ifs;

static Ifs ifs_get(VariableTable* table) {
  Ifs out;
  Variable* v = get_variable(table, _SLIT("IFS"));
  if (v == NULL) {
    out.ifs = string__new(" \t\n");
    out.is_default = true;
    out.no_split = false;
  } else {
    out.ifs = string__from(v->str);
    out.is_default = false;
    out.no_split = (v->str.len == 0);
  }
  return out;
}

static bool ifs_contains(const Ifs* ifs, char c) {
  return memchr(ifs->ifs.str, c, ifs->ifs.len) != NULL;
}

static bool ifs_is_white(const Ifs* ifs, char c) {
  return (c == ' ' || c == '\t' || c == '\n') && ifs_contains(ifs, c);
}

static bool is_delim(const EBuf* e, const Ifs* ifs, size_t i) {
  return e->splittable[i] && ifs_contains(ifs, e->buf[i]);
}

static bool is_white_delim(const EBuf* e, const Ifs* ifs, size_t i) {
  return e->splittable[i] && ifs_is_white(ifs, e->buf[i]);
}

static void split_fields(const EBuf* e, const Ifs* ifs, RangeList* out) {
  if (e->len == 0) return;
  if (ifs->no_split) {
    ranges_push(out, 0, e->len);
    return;
  }

  size_t i = 0;
  while (i < e->len) {
    while (i < e->len && is_white_delim(e, ifs, i)) i++;
    if (i >= e->len) break;

    size_t start = i;
    while (i < e->len && !is_delim(e, ifs, i)) i++;
    ranges_push(out, start, i);

    if (i < e->len) {
      if (is_white_delim(e, ifs, i)) {
        while (i < e->len && is_white_delim(e, ifs, i)) i++;
        if (i < e->len && is_delim(e, ifs, i)) {  /* one trailing non-white delimiter */
          i++;
          while (i < e->len && is_white_delim(e, ifs, i)) i++;
        }
      } else {
        i++;  /* non-white delimiter consumes itself */
        while (i < e->len && is_white_delim(e, ifs, i)) i++;
      }
    }
  }
}

/* ---- globbing ---- */

static bool range_has_glob(const EBuf* e, size_t start, size_t end) {
  for (size_t i = start; i < end; i++) {
    char c = e->buf[i];
    if (e->globbable[i] && (c == '*' || c == '?' || c == '[')) return true;
  }
  return false;
}

static string string_from_range(const EBuf* e, size_t start, size_t end) {
  StringBuilder sb = string_builder__new();
  for (size_t i = start; i < end; i++)
    string_builder__append_char(&sb, e->buf[i]);
  string r = string_builder__to_string(&sb);
  string_builder__free(&sb);
  return r;
}

static void push_field(StringArray* out, string field) {
  array_push(out, &field);
}

/* Globs one field range, pushing matches; falls back to the literal on no match. */
static void glob_range(const EBuf* e, size_t start, size_t end, StringArray* out) {
  if (!range_has_glob(e, start, end)) {
    push_field(out, string_from_range(e, start, end));
    return;
  }

  StringBuilder pat = string_builder__new();
  for (size_t i = start; i < end; i++) {
    char c = e->buf[i];
    bool meta = (c == '*' || c == '?' || c == '[');
    if (meta && e->globbable[i]) {
      string_builder__append_char(&pat, c);
    } else {
      if (meta || c == '\\') string_builder__append_char(&pat, '\\');
      string_builder__append_char(&pat, c);
    }
  }
  string pattern = string_builder__to_string(&pat);
  string_builder__free(&pat);

  glob_t g;
  int rc = glob(pattern.str, 0, NULL, &g);
  string__free(pattern);

  if (rc == 0 && g.gl_pathc > 0) {
    for (size_t i = 0; i < g.gl_pathc; i++)
      push_field(out, string__new(g.gl_pathv[i]));
    globfree(&g);
  } else {
    if (rc == 0) globfree(&g);
    push_field(out, string_from_range(e, start, end));
  }
}

/* ---- public API ---- */

StringArray expand_word_to_fields(VariableTable* table, const Word* w) {
  EBuf e;
  ebuf_init(&e);
  bool had_quoted = false;
  for (size_t i = 0; i < w->size; i++) {
    if (w->data[i].quoted) had_quoted = true;
    emit_segment(&e, table, &w->data[i], i == 0);
  }

  Ifs ifs = ifs_get(table);
  RangeList ranges = {NULL, 0, 0};
  split_fields(&e, &ifs, &ranges);

  StringArray out = create_array(sizeof(string));
  if (ranges.n == 0) {
    if (had_quoted && e.len == 0)
      push_field(&out, string__new(""));
  } else {
    for (size_t i = 0; i < ranges.n; i++)
      glob_range(&e, ranges.data[i].start, ranges.data[i].end, &out);
  }

  rfree(ranges.data);
  string__free(ifs.ifs);
  ebuf_free(&e);
  return out;
}

/* Runs every expansion stage of a word into @p e, short of field splitting and
 * globbing, leaving the per-byte glob-eligibility track intact for callers that
 * need it (string joining and pattern matching). */
static void expand_word_into(VariableTable* table, const Word* w, EBuf* e) {
  for (size_t i = 0; i < w->size; i++)
    emit_segment(e, table, &w->data[i], i == 0);
}

string expand_word_to_string(VariableTable* table, const Word* w) {
  EBuf e;
  ebuf_init(&e);
  expand_word_into(table, w, &e);
  string r = (e.len == 0) ? string__new("") : string_from_range(&e, 0, e.len);
  ebuf_free(&e);
  return r;
}

bool expand_arith(VariableTable* table, const string expr, long long* out) {
  bool ok = true;
  long long v = arith_eval(table, expr, &ok);
  *out = ok ? v : 0;
  return ok;
}

/* Renders an expanded word as an fnmatch(3) pattern: unquoted '*', '?', and '['
 * stay active; the same characters when quoted, and any backslash, are escaped
 * so they match literally. Bracket-expression bodies pass through unchanged. */
static string ebuf_to_pattern(const EBuf* e) {
  StringBuilder sb = string_builder__new();
  for (size_t i = 0; i < e->len; i++) {
    char c = e->buf[i];
    bool meta = (c == '*' || c == '?' || c == '[');
    if (meta && e->globbable[i]) {
      string_builder__append_char(&sb, c);
    } else {
      if (meta || c == '\\') string_builder__append_char(&sb, '\\');
      string_builder__append_char(&sb, c);
    }
  }
  string r = string_builder__to_string(&sb);
  string_builder__free(&sb);
  return r;
}

bool expand_pattern_match(VariableTable* table, const string subject, const Word* pattern) {
  EBuf e;
  ebuf_init(&e);
  expand_word_into(table, pattern, &e);
  string pat = ebuf_to_pattern(&e);
  ebuf_free(&e);
  int rc = fnmatch(pat.str, subject.str, 0);
  string__free(pat);
  return rc == 0;
}

bool expand_regex_match(const string subject, const string regex) {
  regex_t re;
  if (regcomp(&re, regex.str, REG_EXTENDED | REG_NOSUB) != 0) return false;
  int rc = regexec(&re, subject.str, 0, NULL, 0);
  regfree(&re);
  return rc == 0;
}
