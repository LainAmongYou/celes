/*
 * Copyright (c) 2023 Lain Bailey <lain@obsproject.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <ctype.h>
#include "lexer.h"

static const char *astrblank = "";

static bool next_utf32(const char **text, wint_t *ch);

int strref_cmp(const struct strref *str1, const char *str2)
{
	size_t i = 0;

	if (strref_is_empty(str1))
		return (!str2 || !*str2) ? 0 : -1;
	if (!str2)
		str2 = astrblank;

	do {
		char ch1, ch2;

		ch1 = (i < str1->size) ? str1->array[i] : 0;
		ch2 = *str2;

		if (ch1 < ch2)
			return -1;
		else if (ch1 > ch2)
			return 1;
	} while (i++ < str1->size && *str2++);

	return 0;
}

int strref_cmpi(const struct strref *str1, const char *str2)
{
	size_t i = 0;

	if (strref_is_empty(str1))
		return (!str2 || !*str2) ? 0 : -1;
	if (!str2)
		str2 = astrblank;

	do {
		char ch1, ch2;

		ch1 = (i < str1->size) ? (char)toupper(str1->array[i]) : 0;
		ch2 = (char)toupper(*str2);

		if (ch1 < ch2)
			return -1;
		else if (ch1 > ch2)
			return 1;
	} while (i++ < str1->size && *str2++);

	return 0;
}

int strref_cmp_strref(const struct strref *str1, const struct strref *str2)
{
	size_t i = 0;

	if (strref_is_empty(str1))
		return strref_is_empty(str2) ? 0 : -1;
	if (strref_is_empty(str2))
		return -1;

	do {
		char ch1, ch2;

		ch1 = (i < str1->size) ? str1->array[i] : 0;
		ch2 = (i < str2->size) ? str2->array[i] : 0;

		if (ch1 < ch2)
			return -1;
		else if (ch1 > ch2)
			return 1;

		i++;
	} while (i <= str1->size && i <= str2->size);

	return 0;
}

int strref_cmpi_strref(const struct strref *str1, const struct strref *str2)
{
	size_t i = 0;

	if (strref_is_empty(str1))
		return strref_is_empty(str2) ? 0 : -1;
	if (strref_is_empty(str2))
		return -1;

	do {
		char ch1, ch2;

		ch1 = (i < str1->size) ? (char)toupper(str1->array[i]) : 0;
		ch2 = (i < str2->size) ? (char)toupper(str2->array[i]) : 0;

		if (ch1 < ch2)
			return -1;
		else if (ch1 > ch2)
			return 1;

		i++;
	} while (i <= str1->size && i <= str2->size);

	return 0;
}

void strref_trim(struct strref *dst, const struct strref *src)
{
	const char *text      = src->array;
	const char *end       = src->array + src->size;
	const char *new_start = text;
	const char *new_end   = text;

	while (text < end) {
		wint_t ch = 0;

		if (new_start == text) {
			next_utf32(&text, &ch);
			if (iswspace(ch)) {
				new_start = text;
			}

			new_end = text;
		} else {
			next_utf32(&text, &ch);
			if (!iswspace(ch)) {
				new_end = text;
			}
		}
	}

	dst->array = new_start;
	dst->size  = new_end - new_start;
}

/* ------------------------------------------------------------------------- */

bool valid_int_str(const char *str, size_t n)
{
	bool found_num = false;

	if (!str)
		return false;
	if (!*str)
		return false;

	if (!n)
		n = strlen(str);
	if (*str == '-' || *str == '+')
		++str;

	do {
		if (*str > '9' || *str < '0')
			return false;

		found_num = true;
	} while (*++str && --n);

	return found_num;
}

bool valid_float_str(const char *str, size_t n)
{
	bool found_num = false;
	bool found_exp = false;
	bool found_dec = false;

	if (!str)
		return false;
	if (!*str)
		return false;

	if (!n)
		n = strlen(str);
	if (*str == '-' || *str == '+')
		++str;

	do {
		if (*str == '.') {
			if (found_dec || found_exp || !found_num)
				return false;

			found_dec = true;

		} else if (*str == 'e') {
			if (found_exp || !found_num)
				return false;

			found_exp = true;
			found_num = false;

		} else if (*str == '-' || *str == '+') {
			if (!found_exp || !found_num)
				return false;

		} else if (*str > '9' || *str < '0') {
			return false;
		} else {
			found_num = true;
		}
	} while (*++str && --n);

	return found_num;
}

/* ------------------------------------------------------------------------- */

void base_token_connect(struct base_token *dst, const struct base_token *t)
{
	if (!dst->text.array) {
		base_token_copy(dst, t);

	} else if (t->text.array) {
		if (t->text.array > dst->text.array) {
			dst->text.size = t->text.array - dst->text.array + t->text.size;

		} else if (t->text.array == dst->text.array) {
			dst->text.size = dst->text.size > t->text.size ? dst->text.size : t->text.size;

		} else {
			struct base_token temp = *t;
			temp.text.array        = t->text.array;
			temp.text.size         = dst->text.array - t->text.array + dst->text.size;
			*dst                   = temp;
		}
	}
}

/* ------------------------------------------------------------------------- */

void error_data_add(struct error_data *data, const char *file, uint32_t row, uint32_t col, const char *msg, int level)
{
	struct error_item item;

	if (!data)
		return;

	item.file  = file;
	item.row   = row;
	item.col   = col;
	item.level = level;
	item.error = bstrdup(msg);

	da_push_back(data->errors, &item);
}

char *error_data_buildstring(struct error_data *ed)
{
	struct dstr        str;
	struct error_item *items = ed->errors.array;
	size_t             i;

	dstr_init(&str);
	for (i = 0; i < ed->errors.size; i++) {
		struct error_item *item = items + i;
		dstr_catf(&str, "%s (%u, %u): %s\n", item->file, item->row, item->col, item->error);
	}

	return str.array;
}

/* ------------------------------------------------------------------------- */

static bool next_utf32(const char **text, wint_t *ch)
{
	wint_t out = 0;

	*ch = **text;

#define get_next_ch()                                                                                                  \
	do {                                                                                                           \
		*ch = *(++*text);                                                                                      \
		if (!*ch || (*ch & 0xC0) != 0x80)                                                                      \
			return false;                                                                                  \
	} while (false)

	if ((*ch & 0x80) == 0) {
		if (*ch == 0)
			return false;
		++*text;
		return true;

	} else if ((*ch & 0xE0) == 0xC0) {
		out = (*ch & 0x1F) << 6;
		get_next_ch();
		out |= *ch & 0x3F;
		++*text;
		*ch = out;
		return !!out;

	} else if ((*ch & 0xF0) == 0xE0) {
		out = (*ch & 0x0F) << 12;
		get_next_ch();
		out |= (*ch & 0x3F) << 6;
		get_next_ch();
		out |= *ch & 0x3F;
		++*text;
		*ch = out;
		return !!out;

	} else if ((*ch & 0xF8) == 0xF0) {
		out = (*ch & 0x07) << 18;
		get_next_ch();
		out |= (*ch & 0x3F) << 12;
		get_next_ch();
		out |= (*ch & 0x3F) << 6;
		get_next_ch();
		out |= *ch & 0x3F;
		++*text;
		*ch = out;
		return !!out;

	} else if ((*ch & 0xFC) == 0xF8) {
		out = (*ch & 0x03) << 24;
		get_next_ch();
		out = (*ch & 0x3F) << 18;
		get_next_ch();
		out |= (*ch & 0x3F) << 12;
		get_next_ch();
		out |= (*ch & 0x3F) << 6;
		get_next_ch();
		out |= *ch & 0x3F;
		++*text;
		*ch = out;
		return !!out;
	}

#undef get_next_ch

	return false;
}

static inline enum base_token_type get_char_token_type(const wint_t ch)
{
	if (iswspace(ch))
		return BASE_TOKEN_WHITESPACE;
	else if (iswdigit(ch))
		return BASE_TOKEN_DIGIT;
	else if (iswalpha(ch) || ch >= 0x80)
		return BASE_TOKEN_ALPHA;

	return BASE_TOKEN_OTHER;
}

static bool lexer_get_token_internal(struct lexer *lex, struct base_token *token, enum ignore_whitespace iws, bool pop)
{
	const char          *offset            = lex->offset;
	const char          *prev              = offset;
	const char          *token_start       = NULL;
	wint_t               ch                = 0;
	wint_t               out_ch            = 0;
	uint32_t             row               = lex->row;
	uint32_t             col               = lex->col;
	uint32_t             start_row         = row;
	uint32_t             start_col         = col;
	enum base_token_type type              = BASE_TOKEN_NONE;
	enum whitespace_type ws_type           = WHITESPACE_TYPE_UNKNOWN;
	bool                 passed_whitespace = false;
	bool                 passed_newline    = false;
	bool                 ignore_whitespace = (iws == IGNORE_WHITESPACE);
	bool                 stop_parsing      = false;
	size_t               count             = 0;

	if (!offset) {
		return false;
	}

	while (!stop_parsing && next_utf32(&offset, &ch)) {
		enum base_token_type new_type = get_char_token_type(ch);

		if (type == BASE_TOKEN_NONE) {
			bool ignore = false;

			if (new_type == BASE_TOKEN_WHITESPACE) {
				passed_whitespace = true;
				if (is_newline(ch)) {
					passed_newline = true;
				}

				if (ignore_whitespace) {
					ignore = true;
				} else {
					if (is_newline(ch)) {
						ws_type = WHITESPACE_TYPE_NEWLINE;
					} else if (ch == '\t') {
						ws_type = WHITESPACE_TYPE_TAB;
					} else if (ch == ' ' || ch == '\x09') {
						ws_type = WHITESPACE_TYPE_SPACE;
					}
				}
			}

			if (!ignore) {
				out_ch      = ch;
				token_start = prev;
				type        = new_type;
				start_row   = row;
				start_col   = col;

				if (type != BASE_TOKEN_DIGIT && type != BASE_TOKEN_ALPHA) {
					stop_parsing = true;
				}
				count++;
			}
		} else if (type != new_type) {
			offset = prev;
			break;
		} else {
			count++;
		}

		if (is_newline(ch)) {
			if (is_newline_pair(ch, *offset)) {
				offset++;
			}

			row++;
			col = 1;
		} else {
			col++;
		}

		prev = offset;
	}

	if (pop) {
		lex->offset = offset;
		lex->row    = row;
		lex->col    = col;
	}

	if (token_start && offset > token_start) {
		if (token) {
			strref_set(&token->text, token_start, offset - token_start);
			token->ch                = count == 1 ? out_ch : 0;
			token->type              = type;
			token->ws_type           = ws_type;
			token->passed_whitespace = passed_whitespace;
			token->passed_newline    = passed_newline;
			token->row               = start_row;
			token->col               = start_col;
			token->next_offset       = offset;
			token->next_row          = row;
			token->next_col          = col;
		}
		return true;
	}

	return false;
}

bool lexer_peek_token(struct lexer *lex, struct base_token *t, enum ignore_whitespace iws)
{
	return lexer_get_token_internal(lex, t, iws, false);
}

bool lexer_get_token(struct lexer *lex, struct base_token *t, enum ignore_whitespace iws)
{
	return lexer_get_token_internal(lex, t, iws, true);
}

static bool lexer_get_char_internal(struct lexer *lex, struct base_token *token, bool pop)
{
	const char          *offset      = lex->offset;
	const char          *prev        = offset;
	const char          *token_start = offset;
	wint_t               ch          = 0;
	uint32_t             row         = lex->row;
	uint32_t             col         = lex->col;
	uint32_t             start_row   = row;
	uint32_t             start_col   = col;
	enum base_token_type type        = BASE_TOKEN_NONE;
	enum whitespace_type ws_type     = WHITESPACE_TYPE_UNKNOWN;

	if (!next_utf32(&offset, &ch)) {
		return false;
	}

	col++;

	type = get_char_token_type(ch);
	if (type == BASE_TOKEN_WHITESPACE) {
		if (is_newline(ch)) {
			if (is_newline_pair(ch, *offset)) {
				offset++;
			}

			ws_type = WHITESPACE_TYPE_NEWLINE;
			col     = 1;
			row++;

		} else if (ch == '\t') {
			ws_type = WHITESPACE_TYPE_TAB;

		} else if (ch == ' ' || ch == '\x09') {
			ws_type = WHITESPACE_TYPE_SPACE;
		}
	}

	if (pop) {
		lex->offset = offset;
		lex->row    = row;
		lex->col    = col;
	}

	if (token_start && offset > token_start) {
		if (token) {
			strref_set(&token->text, token_start, offset - token_start);
			token->ch                = ch;
			token->type              = type;
			token->ws_type           = ws_type;
			token->passed_whitespace = false;
			token->passed_newline    = false;
			token->row               = start_row;
			token->col               = start_col;
			token->next_offset       = offset;
			token->next_row          = row;
			token->next_col          = col;
		}
		return true;
	}

	return false;
}

bool lexer_peek_char(struct lexer *lex, struct base_token *token)
{
	return lexer_get_char_internal(lex, token, false);
}

bool lexer_get_char(struct lexer *lex, struct base_token *token)
{
	return lexer_get_char_internal(lex, token, true);
}
