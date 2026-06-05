#ifndef __RICKSHELL_WORD_H__
#define __RICKSHELL_WORD_H__
#include <stdbool.h>
#include <stddef.h>
#include "rstring.h"

/*
 * word.h — Pre-expansion representation of a single shell word.
 *
 * A word is kept as an ordered list of segments, each tagged with the quoting
 * context it was lexed in. Preserving that context is what lets the expansion
 * pass obey POSIX rules: the result of a quoted segment is never field-split
 * or globbed, while the result of an unquoted segment is. Collapsing a word to
 * a plain string too early — as quote removal at parse time does — loses the
 * distinction between '$x' (literal) and "$x" (expanded), which is the whole
 * reason this type exists.
 */

/**
 * @brief Kind of a word segment, which selects how the segment is expanded.
 */
typedef enum {
  SEG_LITERAL,  /**< Verbatim text; the text itself is never expanded. */
  SEG_PARAM,    /**< Parameter expansion, stored with its leading '$' (e.g. "$x", "${x:-y}"). */
  SEG_ARITH,    /**< Arithmetic expansion body, without the "$((" / "))" (e.g. "1 + 2"). */
  SEG_COMMAND,  /**< Command substitution body, without the "$(" / ")" or backticks. */
} SegKind;

/**
 * @brief One contiguous piece of a shell word together with its quoting context.
 */
typedef struct {
  SegKind kind;    /**< Selects expansion behavior for @c text. */
  string  text;    /**< Owned segment payload (see SegKind for what it holds). */
  bool    quoted;  /**< True when lexed inside '...' or "..."; suppresses split and glob. */
} WordSeg;

/**
 * @brief A single shell word prior to expansion: an ordered list of segments.
 *
 * Ownership: the word owns @c data and every segment's @c text. Release the
 * whole word with word_free(). A word with zero segments expands to one empty
 * field only when it was written as explicit empty quotes; see word_is_empty().
 */
typedef struct {
  WordSeg* data;  /**< Heap array of segments, or NULL when @c cap is 0. */
  size_t   size;  /**< Number of segments in use. */
  size_t   cap;   /**< Allocated capacity of @c data in segments. */
} Word;

/**
 * @brief Allocates an empty word.
 *
 * @return A new word with no segments. Never NULL — allocation failure aborts
 *         the process, matching the project's other allocators. Free with
 *         word_free().
 */
Word* word_new(void);

/**
 * @brief Frees a word and every segment it owns.
 *
 * @param[in] w  Word to free. NULL is a no-op.
 */
void word_free(Word* w);

/**
 * @brief Appends a segment to a word, taking ownership of @p text.
 *
 * @param[in,out] w       Word to append to. Must not be NULL.
 * @param[in]     kind    Segment kind.
 * @param[in]     text    Segment payload. Ownership transfers to @p w; the
 *                        caller must not free it afterwards.
 * @param[in]     quoted  True if the segment was inside quotes.
 */
void word_push(Word* w, SegKind kind, string text, bool quoted);

/**
 * @brief Reports whether a word carries no segments at all.
 *
 * @param[in] w  Word to inspect. Must not be NULL.
 *
 * @return True when @p w has no segments. A word made only of an empty quoted
 *         literal (e.g. "") still has one segment and is therefore not empty.
 */
bool word_is_empty(const Word* w);

#endif /* __RICKSHELL_WORD_H__ */
