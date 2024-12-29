/*
 * Copyright (c) 2024 Lain Bailey <lain@obsproject.com>
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

#include <inttypes.h>
#include <stdio.h>
#include <wchar.h>

#include "toml.h"
#include "platform.h"
#include "bmem.h"
#include "lexer.h"
#include "dstr.h"
#include "hash.h"

#ifdef ENABLE_TESTS
#include <setjmp.h>
#include <cmocka.h>
#endif

/* ------------------------------------------------------------------------- */
/* Parser                                                                    */

enum parse_error {
	PARSE_SUCCESS,
	PARSE_EOF,
	PARSE_EOL,
	PARSE_UNEXPECTED_TEXT,
	PARSE_UNIMPLEMENTED,
	PARSE_INVALID_IDENTIFIER,
	PARSE_KEY_ALREADY_EXISTS,
};

#define ERROR(error)                                                                                                   \
	do {                                                                                                           \
		error_data_add(&parser->errors, parser->file, token.row, token.col, error, LEX_ERROR);                 \
	} while (false)

#define ERROR_EOF()                                                                                                    \
	do {                                                                                                           \
		ERROR("Unexpected end of file");                                                                       \
		return PARSE_EOF;                                                                                      \
	} while (false)

#define ERROR_EOL()                                                                                                    \
	do {                                                                                                           \
		ERROR("Unexpected end of line");                                                                       \
		return PARSE_EOL;                                                                                      \
	} while (false)

#define ERROR_UNEXPECTED_TEXT()                                                                                        \
	do {                                                                                                           \
		ERROR("Unexpected text");                                                                              \
		return PARSE_UNEXPECTED_TEXT;                                                                          \
	} while (false)

struct toml_value {
	enum toml_type type;
	union {
		char         *string;
		int64_t       integer;
		double        real;
		bool          boolean;
		toml_t       *table;
		toml_array_t *array;
	} data;
};

static void toml_value_free(void *data)
{
	struct toml_value *value = data;
	if (value->type == TOML_TYPE_TABLE) {
		toml_release(value->data.table);
	} else if (value->type == TOML_TYPE_ARRAY) {
		toml_array_release(value->data.array);
	} else if (value->type == TOML_TYPE_STRING) {
		bfree(value->data.string);
	}
}

struct toml_array {
	long refs;
	DARRAY(toml_value_t) values;
};

static inline toml_array_t *toml_array_create(void)
{
	struct toml_array *array = bzalloc(sizeof(*array));
	array->refs              = 1;
	return array;
}

static void toml_array_free(struct toml_array *array)
{
	size_t i;
	for (i = 0; i < array->values.size; i++) {
		toml_value_free(&array->values.array[i]);
	}
	da_free(array->values);
}

struct toml_table {
	long         refs;
	hash_table_t values;
	bool         is_inline;
};

static toml_t *toml_table_create(void)
{
	toml_t *table = bmalloc(sizeof(*table));
	table->refs   = 1;
	hash_table_init(&table->values, sizeof(struct toml_value), toml_value_free);
	return table;
}

static void toml_table_destroy(void *data)
{
	if (data) {
		struct toml_table *table = data;
		hash_table_free(&table->values);
		bfree(table);
	}
}

struct toml_id {
	DARRAY(struct dstr) path;
};

static inline void toml_id_init(struct toml_id *id)
{
	memset(id, 0, sizeof(*id));
}

static inline void toml_id_free(struct toml_id *id)
{
	size_t i = 0;
	for (i = 0; i < id->path.size; i++) {
		dstr_free(&id->path.array[i]);
	}
	da_free(id->path);
	memset(id, 0, sizeof(*id));
}

struct toml_parser {
	const char        *file;
	struct lexer       lexx;
	struct toml_id     cur_table_id;
	struct toml_table *cur_table;
	struct toml_table *root;
	bool               is_table_array;

	struct error_data errors;
};

static inline void toml_parser_init_move(struct toml_parser *parser,
                                         const char         *file,
                                         char               *file_data,
                                         size_t              file_size)
{
	memset(parser, 0, sizeof(*parser));

	parser->file = file;
	lexer_start_move(&parser->lexx, file_data, file_size);
	parser->cur_table = toml_table_create();
	parser->root      = parser->cur_table;
}

static inline void toml_parser_init_static(struct toml_parser *parser,
                                           const char         *file,
                                           const char         *file_data,
                                           size_t              file_size)
{
	memset(parser, 0, sizeof(*parser));

	parser->file = file;
	lexer_start_static(&parser->lexx, file_data, file_size);
	parser->cur_table = toml_table_create();
	parser->root      = parser->cur_table;
}

static inline void toml_parser_free(struct toml_parser *parser)
{
	lexer_free(&parser->lexx);
	toml_id_free(&parser->cur_table_id);
	toml_release(parser->root);
	error_data_free(&parser->errors);
}

static enum parse_error expect_eol(struct toml_parser *parser)
{
	struct base_token token;
	while (lexer_get_token(&parser->lexx, &token, PARSE_WHITESPACE)) {

		if (token.type != BASE_TOKEN_WHITESPACE) {
			ERROR_UNEXPECTED_TEXT();

		} else if (token.ws_type == WHITESPACE_TYPE_NEWLINE) {
			return PARSE_SUCCESS;
		}
	}

	ERROR_EOF();
}

static inline bool pass_whitespace(struct toml_parser *parser)
{
	struct base_token token;
	if (!lexer_peek_token(&parser->lexx, &token, IGNORE_WHITESPACE)) {
		return false;
	}

	lexer_reset_to_token(&parser->lexx, &token);
	return true;
}

static enum parse_error next_char_is_digit(struct toml_parser *parser)
{
	struct base_token token;

	if (!lexer_peek_char(&parser->lexx, &token)) {
		ERROR_EOF();
	}
	if (token.type != BASE_TOKEN_DIGIT) {
		ERROR_UNEXPECTED_TEXT();
	}

	return PARSE_SUCCESS;
}

static enum parse_error expect_next_char(struct toml_parser *parser, wint_t ch, enum ignore_whitespace iw)
{
	struct base_token token;

	if (!lexer_get_token(&parser->lexx, &token, iw)) {
		ERROR_EOF();
	}

	if (token.passed_newline) {
		ERROR_EOL();
	}

	if (token.ch == ch) {
		return PARSE_SUCCESS;
	} else {
		ERROR_UNEXPECTED_TEXT();
	}
}

static enum parse_error parse_escape_code(struct toml_parser *parser, struct dstr *str)
{
	struct base_token token;
	size_t            i;

	if (lexer_get_char(&parser->lexx, &token)) {
		char ch = token.ch;

		if (ch == 'b') {
			dstr_cat_ch(str, '\b');
		} else if (ch == 't') {
			dstr_cat_ch(str, '\t');
		} else if (ch == 'n') {
			dstr_cat_ch(str, '\n');
		} else if (ch == 'f') {
			dstr_cat_ch(str, '\f');
		} else if (ch == 'r') {
			dstr_cat_ch(str, '\r');
		} else if (ch == '"') {
			dstr_cat_ch(str, '"');
		} else if (ch == '\\') {
			dstr_cat_ch(str, '\\');

		} else if (ch == 'u' || ch == 'U') {
			ERROR("Unicode escape codes currently unsupported");
			return PARSE_UNIMPLEMENTED;

		} else {
			ERROR_UNEXPECTED_TEXT();
		}

		return PARSE_SUCCESS;
	}

	ERROR_EOF();
}

static enum parse_error parse_multiline_string(struct toml_parser *parser, struct dstr *str)
{
	struct base_token token;
	enum parse_error  error;

	lexer_get_token(&parser->lexx, NULL, PARSE_WHITESPACE); /* " */
	lexer_get_token(&parser->lexx, NULL, PARSE_WHITESPACE); /* " */

	while (lexer_get_token(&parser->lexx, &token, PARSE_WHITESPACE)) {
		if (token.ch == '\\') {
			error = parse_escape_code(parser, str);
			if (error != PARSE_SUCCESS) {
				return error;
			}
		} else if (astrcmp_n(token.text.array, "\"\"\"", 3) == 0) {
			lexer_get_char(&parser->lexx, NULL); /* " */
			lexer_get_char(&parser->lexx, NULL); /* " */
			return PARSE_SUCCESS;
		} else {
			dstr_cat_strref(str, &token.text);
		}
	}

	ERROR_EOF();
}

static enum parse_error parse_string(struct toml_parser *parser, struct dstr *str)
{
	struct base_token token;
	enum parse_error  error;

	lexer_get_token(&parser->lexx, &token, IGNORE_WHITESPACE); /* known delimiter */

	if (astrcmp_n(token.text.array, "\"\"\"", 3) == 0) {
		return parse_multiline_string(parser, str);
	}

	while (lexer_get_token(&parser->lexx, &token, PARSE_WHITESPACE)) {
		if (token.ws_type == WHITESPACE_TYPE_NEWLINE) {
			ERROR_EOL();
		}
		if (token.ch == '\\') {
			error = parse_escape_code(parser, str);
			if (error != PARSE_SUCCESS) {
				return error;
			}
		} else if (token.ch == '"') {
			return PARSE_SUCCESS;
		} else {
			dstr_cat_strref(str, &token.text);
		}
	}

	ERROR_EOF();
}

static enum parse_error parse_multiline_string_literal(struct toml_parser *parser, struct dstr *str)
{
	struct base_token token;

	lexer_get_token(&parser->lexx, NULL, PARSE_WHITESPACE); /* ' */
	lexer_get_token(&parser->lexx, NULL, PARSE_WHITESPACE); /* ' */

	while (lexer_get_token(&parser->lexx, &token, PARSE_WHITESPACE)) {
		if (astrcmp_n(token.text.array, "'''", 3) == 0) {
			lexer_get_char(&parser->lexx, NULL);   /* ' */
			lexer_get_char(&parser->lexx, &token); /* ' */
			return PARSE_SUCCESS;
		}
		dstr_cat_strref(str, &token.text);
	}

	ERROR_EOF();
}

static enum parse_error parse_string_literal(struct toml_parser *parser, struct dstr *str)
{
	struct base_token token;
	lexer_get_token(&parser->lexx, &token, IGNORE_WHITESPACE); /* known delimiter */

	if (astrcmp_n(token.text.array, "'''", 3) == 0) {
		return parse_multiline_string_literal(parser, str);
	}

	while (lexer_get_token(&parser->lexx, &token, PARSE_WHITESPACE)) {
		if (token.ws_type == WHITESPACE_TYPE_NEWLINE) {
			ERROR_EOL();
		}
		if (token.ch == '\'') {
			return PARSE_SUCCESS;
		}
		dstr_cat_strref(str, &token.text);
	}

	ERROR_EOF();
}

static enum parse_error parse_number(struct toml_parser *parser, struct toml_value *value)
{
	struct base_token token;
	struct dstr       str            = {0};
	bool              found_decimal  = false;
	bool              found_exponent = false;
	bool              found_number   = false;
	wint_t            base           = 10;
	enum parse_error  err;

	value->type = TOML_TYPE_INTEGER;

	if (!lexer_peek_token(&parser->lexx, &token, IGNORE_WHITESPACE)) {
		ERROR_EOF();
	}

	if (token.ch == '-' || token.ch == '+') {
		lexer_pass_token(&parser->lexx, &token);

		if (token.ch == '-') {
			dstr_cat_ch(&str, '-');
		}

		if (!lexer_peek_token(&parser->lexx, &token, PARSE_WHITESPACE)) {
			ERROR_EOF();
		}
	}

	if (astrcmpi_n(token.text.array, "0b", 2) == 0) {
		base = 2;
	} else if (astrcmpi_n(token.text.array, "0o", 2) == 0) {
		base = 8;
	} else if (astrcmpi_n(token.text.array, "0x", 2) == 0) {
		base = 16;
	}

	if (base != 10) {
		lexer_pass_token(&parser->lexx, &token);
		lexer_get_char(&parser->lexx, NULL);

	} else if (strref_cmp(&token.text, "inf") == 0) {
		ERROR("inf is unsupported");
		return PARSE_UNIMPLEMENTED;

	} else if (strref_cmp(&token.text, "nan") == 0) {
		ERROR("nan is unsupported");
		return PARSE_UNIMPLEMENTED;
	}

	while (lexer_peek_char(&parser->lexx, &token)) {
		if (token.type == BASE_TOKEN_WHITESPACE) {
			if (!dstr_is_empty(&str)) {
				break;
			}

		} else if (token.type == BASE_TOKEN_DIGIT) {
			found_number = true;
			dstr_cat_strref(&str, &token.text);

			if ((token.ch - '0') >= base) {
				ERROR_UNEXPECTED_TEXT();
			}

		} else if (token.type == BASE_TOKEN_ALPHA) {
			/* parse exponent */
			if (base == 10 && found_number && !found_exponent && towlower(token.ch) == 'e') {
				found_exponent = true;
				dstr_cat_ch(&str, 'e');
				lexer_pass_token(&parser->lexx, &token);

				/* parse +/- if any */
				if (!lexer_peek_char(&parser->lexx, &token)) {
					ERROR_EOF();
				}
				if (token.ch == '+' || token.ch == '-') {
					lexer_pass_token(&parser->lexx, &token);
					dstr_cat_strref(&str, &token.text);
				}

				err = next_char_is_digit(parser);
				if (err != PARSE_SUCCESS) {
					return err;
				}

				continue;

			} else if (base == 16) { /* if hex, parse A-F */
				wint_t ch = towlower(token.ch);
				if (ch >= 'a' && ch <= 'f') {
					dstr_cat_strref(&str, &token.text);
				} else {
					ERROR_UNEXPECTED_TEXT();
				}

			} else {
				ERROR_UNEXPECTED_TEXT();
			}

		} else if (token.type == BASE_TOKEN_OTHER) {
			/* parse decimal */
			if (token.ch == '.' && base == 10 && found_number && !found_decimal && !found_exponent) {
				found_decimal = true;
				dstr_cat_ch(&str, '.');
				lexer_pass_token(&parser->lexx, &token);

				err = next_char_is_digit(parser);
				if (err != PARSE_SUCCESS) {
					return err;
				}
				continue;

			} else if (token.ch == '_') { /* parse underscore usage */
				lexer_pass_token(&parser->lexx, &token);

				err = next_char_is_digit(parser);
				if (err != PARSE_SUCCESS) {
					return err;
				}
				continue;

			} else {
				ERROR_UNEXPECTED_TEXT();
			}
		}

		lexer_pass_token(&parser->lexx, &token);
	}

	if (dstr_is_empty(&str)) {
		ERROR_EOF();
	}

	if (found_decimal || found_exponent) { /* float */
		value->type      = TOML_TYPE_REAL;
		value->data.real = strtod(str.array, NULL);
	} else { /* integer */
		value->type         = TOML_TYPE_INTEGER;
		value->data.integer = strtoll(str.array, NULL, base);
	}

	return PARSE_SUCCESS;
}

static void parse_comment(struct toml_parser *parser)
{
	struct base_token token;

	while (lexer_get_token(&parser->lexx, &token, PARSE_WHITESPACE)) {
		if (token.ws_type == WHITESPACE_TYPE_NEWLINE) {
			return;
		}
	}
}

static enum parse_error parse_singular_identifier(struct toml_parser *parser, struct dstr *id, wint_t delimiter)
{
	struct base_token token;
	bool              first = true;

	if (!lexer_peek_token(&parser->lexx, &token, IGNORE_WHITESPACE)) {
		goto eof;
	}

	if (token.ch == '"') {
		return parse_string(parser, id);

	} else if (token.ch == '\'') {
		return parse_string_literal(parser, id);
	}

	while (lexer_peek_token(&parser->lexx, &token, IGNORE_WHITESPACE)) {
		char ch = token.ch;

		if (token.passed_newline) {
			ERROR_EOL();
		}
		if (!first && token.passed_whitespace) {
			return PARSE_SUCCESS;
		}
		if (token.ch == delimiter) {
			return PARSE_SUCCESS;
		}
		if (token.ch == '.') {
			return PARSE_SUCCESS;
		}

		if (token.type != BASE_TOKEN_ALPHA && token.type != BASE_TOKEN_DIGIT && ch != '_' && ch != '-') {
			ERROR_UNEXPECTED_TEXT();
		}

		first = false;

		lexer_get_token(&parser->lexx, NULL, IGNORE_WHITESPACE);
		dstr_cat_strref(id, &token.text);
	}

eof:
	ERROR_EOF();
}

static enum parse_error parse_identifier(struct toml_parser *parser, struct toml_id *id, wint_t delimiter)
{
	struct base_token token;
	struct dstr       sub_id = {0};
	enum parse_error  error;

	if (delimiter == '=' && !pass_whitespace(parser)) {
		ERROR_EOF();
	}

	while ((error = parse_singular_identifier(parser, &sub_id, delimiter)) == PARSE_SUCCESS) {
		da_push_back(id->path, &sub_id);
		dstr_init(&sub_id);

		lexer_peek_token(&parser->lexx, &token, IGNORE_WHITESPACE);
		if (token.passed_newline) {
			ERROR_EOL();
		}

		if (token.ch == '.') {
			lexer_get_token(&parser->lexx, NULL, IGNORE_WHITESPACE);
			if (!lexer_peek_token(&parser->lexx, &token, IGNORE_WHITESPACE)) {
				ERROR_EOF();
			}
			if (token.passed_newline) {
				ERROR_EOL();
			}
		} else {
			if (token.passed_whitespace && token.ch != delimiter) {
				ERROR_UNEXPECTED_TEXT();
			}
			return PARSE_SUCCESS;
		}
	}

	return error;
}

static enum parse_error parse_value(struct toml_parser *parser, struct toml_value *value)
{
	struct base_token token;

	if (!lexer_peek_token(&parser->lexx, &token, IGNORE_WHITESPACE)) {
		ERROR_EOF();
	}
	if (token.passed_newline) {
		ERROR_EOL();
	}

	if (strref_cmp(&token.text, "true") == 0) {
		value->type         = TOML_TYPE_BOOLEAN;
		value->data.boolean = true;
		return PARSE_SUCCESS;

	} else if (strref_cmp(&token.text, "false") == 0) {
		value->type         = TOML_TYPE_BOOLEAN;
		value->data.boolean = false;
		return PARSE_SUCCESS;

	} else if (token.ch == '[') {
		/* TODO */
		return PARSE_UNIMPLEMENTED;

	} else if (token.ch == '{') {
		/* TODO */
		return PARSE_UNIMPLEMENTED;

	} else if (token.ch == '"') {
		struct dstr str = {0};

		enum parse_error error = parse_string(parser, &str);
		if (error != PARSE_SUCCESS) {
			return error;
		}
		value->type        = TOML_TYPE_STRING;
		value->data.string = str.array;
		return PARSE_SUCCESS;

	} else if (token.ch == '\'') {
		struct dstr str = {0};

		enum parse_error error = parse_string_literal(parser, &str);
		if (error != PARSE_SUCCESS) {
			return error;
		}
		value->type        = TOML_TYPE_STRING;
		value->data.string = str.array;
		return PARSE_SUCCESS;

	} else if (token.ch == '+' || token.ch == '-') {
		return parse_number(parser, value);

	} else if (strref_cmp(&token.text, "inf") == 0) {
		ERROR("inf is unsupported");
		return PARSE_UNIMPLEMENTED;

	} else if (strref_cmp(&token.text, "nan") == 0) {
		ERROR("nan is unsupported");
		return PARSE_UNIMPLEMENTED;

	} else if (token.type == BASE_TOKEN_DIGIT) {
		return parse_number(parser, value);
	}

	ERROR_UNEXPECTED_TEXT();
}

static bool get_subtable_and_subkey(struct toml_parser *parser,
                                    struct toml_table  *table,
                                    struct toml_id     *id,
                                    struct toml_table **p_subtable,
                                    struct dstr       **p_subkey)
{
	struct toml_table *cur_subtable = table;
	struct dstr       *cur_subkey   = &id->path.array[0];
	size_t             i;

	for (i = 1; i < id->path.size; i++) {
		struct dstr       *key          = &id->path.array[i];
		struct toml_value *cur_subvalue = hash_table_get(&cur_subtable->values, cur_subkey->array);

		if (cur_subvalue) {
			if (cur_subvalue->type != TOML_TYPE_TABLE) {
				return false;
			}
		} else {
			struct toml_value new_subvalue;
			new_subvalue.type       = TOML_TYPE_TABLE;
			new_subvalue.data.table = toml_table_create();
			cur_subvalue            = hash_table_set_n(
                                &cur_subtable->values, cur_subkey->array, cur_subkey->size, &new_subvalue);
		}

		cur_subtable = cur_subvalue->data.table;
		cur_subkey   = key;
	}

	*p_subtable = cur_subtable;
	*p_subkey   = cur_subkey;

	return true;
}

static bool insert_table_header(struct toml_parser *parser, struct toml_table *root)
{
	struct toml_table *cur_subtable = root;
	struct toml_id    *id           = &parser->cur_table_id;
	struct dstr       *cur_subkey   = &id->path.array[0];
	struct toml_value  new_value;
	size_t             i;

	new_value.type       = TOML_TYPE_TABLE;
	new_value.data.table = parser->cur_table;

	for (i = 1; i < id->path.size - 1; i++) {
		struct dstr       *key          = &id->path.array[i];
		struct toml_value *cur_subvalue = hash_table_get(&cur_subtable->values, cur_subkey->array);

		if (cur_subvalue) {
			if (cur_subvalue->type == TOML_TYPE_ARRAY) {
				struct toml_array *array = cur_subvalue->data.array;

				if (!array->values.size) {
					return false;
				}

				cur_subvalue = da_end(array->values);

			} else if (cur_subvalue->type != TOML_TYPE_TABLE) {
				return false;
			}
		} else {
			struct toml_value new_subvalue;
			new_subvalue.type       = TOML_TYPE_TABLE;
			new_subvalue.data.table = toml_table_create();
			cur_subvalue            = hash_table_set_n(
                                &cur_subtable->values, cur_subkey->array, cur_subkey->size, &new_subvalue);
		}

		cur_subtable = cur_subvalue->data.table;
		cur_subkey   = key;
	}

	if (parser->is_table_array) {
		struct toml_value *array_val = hash_table_get(&cur_subtable->values, cur_subkey->array);
		struct toml_array *array     = array_val->data.array;

		if (array_val->type != TOML_TYPE_ARRAY || array->values.array[0].type != TOML_TYPE_TABLE) {
			return false;
		}

		da_push_back(array->values, &new_value);
	} else {
		if (hash_table_get(&cur_subtable->values, cur_subkey->array)) {
			return false;
		}

		hash_table_set_n(&cur_subtable->values, cur_subkey->array, cur_subkey->size, &new_value);
	}

	parser->cur_table = NULL;
	return true;
}

static enum parse_error parse_key_pair(struct toml_parser *parser, struct toml_table *table)
{
	struct base_token  token;
	struct toml_id     id       = {0};
	struct toml_value  value    = {0};
	struct toml_table *subtable = NULL;
	struct dstr       *subkey   = NULL;
	enum parse_error   error;

	error = parse_identifier(parser, &id, '=');
	if (error != PARSE_SUCCESS) {
		return error;
	}

	error = expect_next_char(parser, '=', IGNORE_WHITESPACE);
	if (error != PARSE_SUCCESS) {
		return error;
	}

	if (!lexer_peek_token(&parser->lexx, &token, IGNORE_WHITESPACE)) {
		ERROR_EOF();
	}
	if (token.passed_newline) {
		ERROR_EOL();
	}

	error = parse_value(parser, &value);
	if (error != PARSE_SUCCESS) {
		return error;
	}

	subtable = table;
	subkey   = &id.path.array[0];

	if (!get_subtable_and_subkey(parser, table, &id, &subtable, &subkey)) {
		ERROR("Invalid identifier, indentifier name already in use by key of the same name"
		      "(Improve this error later)");
		error = PARSE_INVALID_IDENTIFIER;
		goto fail;
	}

	struct toml_value *existing = toml_table_get_value(subtable, subkey->array);
	if (existing) {
		ERROR("Key already exists (Improve this error later)");
		error = PARSE_KEY_ALREADY_EXISTS;
		goto fail;
	}

	hash_table_set_n(&subtable->values, subkey->array, subkey->size, &value);
	memset(&value, 0, sizeof(value));

fail:
	toml_id_free(&id);
	toml_value_free(&value);
	return error;
}

static enum parse_error parse_table_header(struct toml_parser *parser, struct toml_table *root)
{
	struct base_token  token;
	struct toml_id     id          = {0};
	struct toml_table *subtable    = NULL;
	struct dstr       *subkey      = NULL;
	bool               table_array = false;
	enum parse_error   error;

	lexer_get_token(&parser->lexx, NULL, IGNORE_WHITESPACE); /* '[' */

	if (!lexer_peek_token(&parser->lexx, &token, IGNORE_WHITESPACE)) {
		ERROR_EOF();
	}

	if (token.ch == '[') { /* table array */
		table_array = true;
		if (!lexer_get_token(&parser->lexx, &token, IGNORE_WHITESPACE)) {
			ERROR_EOF();
		}
	}

	error = parse_identifier(parser, &id, ']');
	if (error != PARSE_SUCCESS) {
		return error;
	}

	if (table_array) {
		error = expect_next_char(parser, ']', IGNORE_WHITESPACE);
		if (error != PARSE_SUCCESS) {
			goto fail;
		}
	}

	error = expect_next_char(parser, ']', IGNORE_WHITESPACE);
	if (error != PARSE_SUCCESS) {
		goto fail;
	}

	if (parser->cur_table != root) {
		if (!insert_table_header(parser, root)) {
			ERROR("Invalid table assignment, key already in use by non-table "
			      "(Improve this error later)");
			error = PARSE_INVALID_IDENTIFIER;
			goto fail;
		}
	}

	parser->cur_table    = toml_table_create();
	parser->cur_table_id = id;
	return PARSE_SUCCESS;

fail:
	toml_id_free(&id);
	return error;
}

static enum parse_error parse_toml_data(struct toml_parser *parser)
{
	struct strref     table_key;
	struct base_token token;
	enum parse_error  error;

	while (lexer_peek_token(&parser->lexx, &token, IGNORE_WHITESPACE)) {
		struct toml_table *table;

		if (token.ch == '[') {
			error = parse_table_header(parser, parser->root);
			if (error != PARSE_SUCCESS) {
				return error;
			}
			continue;

		} else if (token.ch == '#') {
			parse_comment(parser);
			continue;
		}

		error = parse_key_pair(parser, parser->cur_table);
		if (error != PARSE_SUCCESS) {
			return error;
		}
	}

	insert_table_header(parser, parser->root);
	return PARSE_SUCCESS;
}

/* ------------------------------------------------------------------------- */
/* Tables                                                                    */

int toml_open(toml_t **toml, const char *file, char **errors)
{
	struct toml_parser parser;
	char              *file_data;
	size_t             size;
	bool               success;

	if (!toml) {
		return TOML_ERROR;
	}
	file_data = os_quick_read_utf8_file(file, &size);
	if (!file_data) {
		return TOML_FILE_NOT_FOUND;
	}
	if (!*file_data) {
		return TOML_SUCCESS;
	}

	toml_parser_init_move(&parser, file, file_data, size);
	success = parse_toml_data(&parser) != PARSE_SUCCESS;

	if (!success) {
		if (errors && parser.errors.errors.size) {
			*errors = error_data_buildstring(&parser.errors);
		}
		*toml = NULL;
	} else {
		*toml = toml_addref(parser.root);
	}

	toml_parser_free(&parser);
	return success ? TOML_SUCCESS : TOML_ERROR;
}

toml_t *toml_addref(toml_t *toml)
{
	if (toml && toml->refs) {
		toml->refs++;
		return toml;
	}

	return NULL;
}

long toml_release(toml_t *toml)
{
	if (!toml) {
		return 0;
	}

	if (--toml->refs == 0) {
		toml_table_destroy(toml);
		return 0;
	}

	return toml->refs;
}

size_t toml_table_get_pair_count(toml_t *toml)
{
	return toml ? toml->values.size : 0;
}

struct toml_pair toml_table_get_pair(toml_t *toml, size_t idx)
{
	struct toml_pair pair;
	pair.value = hash_table_get_idx(&toml->values, idx, &pair.key);
	return pair;
}

toml_value_t *toml_table_get_value(toml_t *toml, const char *key)
{
	return hash_table_get(&toml->values, key);
}

enum toml_type toml_table_get_type(toml_t *toml, const char *key)
{
	struct toml_value *value = hash_table_get(&toml->values, key);
	return value ? value->type : TOML_TYPE_INVALID;
}

const char *toml_table_get_string(toml_t *toml, const char *key)
{
	struct toml_value *value = hash_table_get(&toml->values, key);
	return value ? (value->type == TOML_TYPE_STRING ? value->data.string : NULL) : NULL;
}

int64_t toml_table_get_int(toml_t *toml, const char *key)
{
	struct toml_value *value = hash_table_get(&toml->values, key);
	return value ? (value->type == TOML_TYPE_INTEGER ? value->data.integer : 0) : 0;
}

bool toml_table_get_bool(toml_t *toml, const char *key)
{
	struct toml_value *value = hash_table_get(&toml->values, key);
	return value ? (value->type == TOML_TYPE_BOOLEAN ? value->data.boolean : false) : false;
}

double toml_table_get_double(toml_t *toml, const char *key)
{
	struct toml_value *value = hash_table_get(&toml->values, key);
	return value ? (value->type == TOML_TYPE_REAL ? value->data.real : 0.0) : 0.0;
}

toml_t *toml_table_get_table(toml_t *toml, const char *key)
{
	struct toml_value *value = hash_table_get(&toml->values, key);
	return value ? (value->type == TOML_TYPE_TABLE ? value->data.table : NULL) : NULL;
}

toml_array_t *toml_table_get_array(toml_t *toml, const char *key)
{
	struct toml_value *value = hash_table_get(&toml->values, key);
	return value ? (value->type == TOML_TYPE_ARRAY ? value->data.array : NULL) : NULL;
}

bool toml_table_has_value(toml_t *toml, const char *key)
{
	return !!hash_table_get(&toml->values, key);
}

/* ------------------------------------------------------------------------- */
/* Arrays                                                                    */

toml_array_t *toml_array_addref(toml_array_t *array)
{
	if (array && array->refs) {
		array->refs++;
		return array;
	}

	return NULL;
}

long toml_array_release(toml_array_t *array)
{
	if (array) {
		if (--array->refs) {
			return array->refs;
		}

		toml_array_free(array);
		bfree(array);
	}

	return 0;
}

size_t toml_array_count(toml_array_t *array)
{
	return array ? array->values.size : 0;
}

static inline struct toml_value *toml_array_get_value_inline(toml_array_t *array, size_t idx)
{
	return (array && idx < array->values.size) ? &array->values.array[idx] : NULL;
}

toml_value_t *toml_array_get_value(toml_array_t *array, size_t idx)
{
	return toml_array_get_value_inline(array, idx);
}

const char *toml_array_get_string(toml_array_t *array, size_t idx)
{
	struct toml_value *value = toml_array_get_value_inline(array, idx);
	return (value && value->type == TOML_TYPE_STRING) ? value->data.string : NULL;
}

int64_t toml_array_get_int(toml_array_t *array, size_t idx)
{
	struct toml_value *value = toml_array_get_value_inline(array, idx);
	return (value && value->type == TOML_TYPE_INTEGER) ? value->data.integer : 0;
}

bool toml_array_get_bool(toml_array_t *array, size_t idx)
{
	struct toml_value *value = toml_array_get_value_inline(array, idx);
	return (value && value->type == TOML_TYPE_BOOLEAN) ? value->data.boolean : false;
}

double toml_array_get_double(toml_array_t *array, size_t idx)
{
	struct toml_value *value = toml_array_get_value_inline(array, idx);
	return (value && value->type == TOML_TYPE_REAL) ? value->data.real : 0.0;
}

toml_t *toml_array_get_table(toml_array_t *array, size_t idx)
{
	struct toml_value *value = toml_array_get_value_inline(array, idx);
	return (value && value->type == TOML_TYPE_TABLE) ? value->data.table : NULL;
}

toml_array_t *toml_array_get_array(toml_array_t *array, size_t idx)
{
	struct toml_value *value = toml_array_get_value_inline(array, idx);
	return (value && value->type == TOML_TYPE_ARRAY) ? value->data.array : NULL;
}

/* ------------------------------------------------------------------------- */
/* Items                                                                     */

enum toml_type toml_value_get_type(toml_value_t *value)
{
	return value ? value->type : TOML_TYPE_INVALID;
}

const char *toml_value_get_string(toml_value_t *value)
{
	return (value && value->type == TOML_TYPE_STRING) ? value->data.string : NULL;
}

int64_t toml_value_get_int(toml_value_t *value)
{
	return (value && value->type == TOML_TYPE_INTEGER) ? value->data.integer : 0;
}

bool toml_value_get_bool(toml_value_t *value)
{
	return (value && value->type == TOML_TYPE_BOOLEAN) ? value->data.boolean : false;
}

double toml_value_get_double(toml_value_t *value)
{
	return (value && value->type == TOML_TYPE_REAL) ? value->data.real : 0.0;
}

toml_t *toml_value_get_table(toml_value_t *value)
{
	return (value && value->type == TOML_TYPE_TABLE) ? value->data.table : NULL;
}

toml_array_t *toml_value_get_array(toml_value_t *value)
{
	return (value && value->type == TOML_TYPE_ARRAY) ? value->data.array : NULL;
}

/* ------------------------------------------------------------------------- */
/* Sub-table access (just helper functions, usually used for the base table) */

static struct toml_value *toml_get_subtable_value_inline(toml_t        *toml,
                                                         const char    *table,
                                                         const char    *key,
                                                         enum toml_type type)
{
	struct toml_value *value = hash_table_get(&toml->values, table);
	value = (value->type == TOML_TYPE_TABLE) ? hash_table_get(&value->data.table->values, key) : NULL;
	return (value->type == type) ? value : NULL;
}

const char *toml_get_string(toml_t *toml, const char *table, const char *key)
{
	if (!toml) {
		return NULL;
	}
	struct toml_value *value = toml_get_subtable_value_inline(toml, table, key, TOML_TYPE_STRING);
	return value ? value->data.string : NULL;
}

int64_t toml_get_int(toml_t *toml, const char *table, const char *key)
{
	if (!toml) {
		return 0;
	}
	struct toml_value *value = toml_get_subtable_value_inline(toml, table, key, TOML_TYPE_INTEGER);
	return value ? value->data.integer : 0;
}

bool toml_get_bool(toml_t *toml, const char *table, const char *key)
{
	if (!toml) {
		return false;
	}
	struct toml_value *value = toml_get_subtable_value_inline(toml, table, key, TOML_TYPE_BOOLEAN);
	return value ? value->data.integer : false;
}

double toml_get_double(toml_t *toml, const char *table, const char *key)
{
	if (!toml) {
		return 0.0;
	}
	struct toml_value *value = toml_get_subtable_value_inline(toml, table, key, TOML_TYPE_REAL);
	return value ? value->data.integer : 0.0;
}

toml_t *toml_get_table(toml_t *toml, const char *table, const char *key)
{
	if (!toml) {
		return NULL;
	}
	struct toml_value *value = toml_get_subtable_value_inline(toml, table, key, TOML_TYPE_TABLE);
	return value ? value->data.table : NULL;
}

toml_array_t *toml_get_array(toml_t *toml, const char *table, const char *key)
{
	if (!toml) {
		return NULL;
	}
	struct toml_value *value = toml_get_subtable_value_inline(toml, table, key, TOML_TYPE_ARRAY);
	return value ? value->data.array : NULL;
}

bool toml_has_user_value(toml_t *toml, const char *table, const char *key)
{
	struct toml_value *value = hash_table_get(&toml->values, table);
	value = (value->type == TOML_TYPE_TABLE) ? hash_table_get(&value->data.table->values, key) : NULL;
	return !!value;
}

#ifdef ENABLE_TESTS

/* functions to test:
 * - expect_eol
 * - parse_escape_code
 * - expect_next_char
 * - parse_multiline_string
 * - parse_string
 * - parse_multiline_string_literal
 * - parse_string_literal
 * - parse_number
 *
 * - parse_singular_identifier
 * - parse_identifier
 * - parse_value
 * - parse_key_pair
 * - parse_table_header
 * - insert_table_header
 * - get_subtable_and_subkey
 * - parse_toml_data */

static inline void parser_mock_destroy(struct toml_parser *parser)
{
	if (parser) {
		toml_parser_free(parser);
		bfree(parser);
	}
}

static void generate_parser_mock(struct toml_parser **p_parser, const char *string)
{
	struct toml_parser *parser = *p_parser;

	if (parser) {
		toml_parser_free(parser);
	} else {
		parser = bzalloc(sizeof(*parser));
	}

	toml_parser_init_static(parser, "test", string, strlen(string));
	*p_parser = parser;
}

void toml_test_eol(void **state)
{
	struct toml_parser *parser = NULL;

	generate_parser_mock(&parser, "\n");
	assert_int_equal(expect_eol(parser), PARSE_SUCCESS);
	generate_parser_mock(&parser, "x");
	assert_int_equal(expect_eol(parser), PARSE_UNEXPECTED_TEXT);
	generate_parser_mock(&parser, "");
	assert_int_equal(expect_eol(parser), PARSE_EOF);

	parser_mock_destroy(parser);

	UNUSED_PARAMETER(state);
}

void toml_test_parse_escape_code(void **state)
{
	struct toml_parser *parser = NULL;
	struct dstr         out    = {0};

	generate_parser_mock(&parser, "b");
	assert_int_equal(parse_escape_code(parser, &out), PARSE_SUCCESS);
	assert_int_equal(dstr_cmp(&out, "\b"), 0);
	dstr_clear(&out);

	generate_parser_mock(&parser, "t");
	assert_int_equal(parse_escape_code(parser, &out), PARSE_SUCCESS);
	assert_int_equal(dstr_cmp(&out, "\t"), 0);
	dstr_clear(&out);

	generate_parser_mock(&parser, "n");
	assert_int_equal(parse_escape_code(parser, &out), PARSE_SUCCESS);
	assert_int_equal(dstr_cmp(&out, "\n"), 0);
	dstr_clear(&out);

	generate_parser_mock(&parser, "f");
	assert_int_equal(parse_escape_code(parser, &out), PARSE_SUCCESS);
	assert_int_equal(dstr_cmp(&out, "\f"), 0);
	dstr_clear(&out);

	generate_parser_mock(&parser, "r");
	assert_int_equal(parse_escape_code(parser, &out), PARSE_SUCCESS);
	assert_int_equal(dstr_cmp(&out, "\r"), 0);
	dstr_clear(&out);

	generate_parser_mock(&parser, "\"");
	assert_int_equal(parse_escape_code(parser, &out), PARSE_SUCCESS);
	assert_int_equal(dstr_cmp(&out, "\""), 0);
	dstr_clear(&out);

	generate_parser_mock(&parser, "\\");
	assert_int_equal(parse_escape_code(parser, &out), PARSE_SUCCESS);
	assert_int_equal(dstr_cmp(&out, "\\"), 0);

	generate_parser_mock(&parser, "u");
	assert_int_equal(parse_escape_code(parser, &out), PARSE_UNIMPLEMENTED);
	generate_parser_mock(&parser, "v");
	assert_int_equal(parse_escape_code(parser, &out), PARSE_UNEXPECTED_TEXT);
	generate_parser_mock(&parser, "");
	assert_int_equal(parse_escape_code(parser, &out), PARSE_EOF);

	dstr_free(&out);
	parser_mock_destroy(parser);

	UNUSED_PARAMETER(state);
}

void toml_test_expect_next_char(void **state)
{
	struct toml_parser *parser = NULL;

	generate_parser_mock(&parser, "x");
	assert_int_equal(expect_next_char(parser, 'b', PARSE_WHITESPACE), PARSE_UNEXPECTED_TEXT);
	generate_parser_mock(&parser, "b");
	assert_int_equal(expect_next_char(parser, 'b', PARSE_WHITESPACE), PARSE_SUCCESS);
	generate_parser_mock(&parser, "\nb");
	assert_int_equal(expect_next_char(parser, 'b', IGNORE_WHITESPACE), PARSE_EOL);
	generate_parser_mock(&parser, "");
	assert_int_equal(expect_next_char(parser, 'b', IGNORE_WHITESPACE), PARSE_EOF);

	parser_mock_destroy(parser);

	UNUSED_PARAMETER(state);
}

void toml_test_parse_multiline_string(void **state)
{
	struct toml_parser *parser = NULL;
	struct dstr         out    = {0};

	generate_parser_mock(&parser, "\"\"\"bla\n\"\\\"bla\"\"\"");
	assert_int_equal(parse_string(parser, &out), PARSE_SUCCESS);
	assert_int_equal(dstr_cmp(&out, "bla\n\"\"bla"), 0);

	/* take off a " from the end to test EOF */
	generate_parser_mock(&parser, "\"\"\"bla\nbla\"\"");
	assert_int_equal(parse_string(parser, &out), PARSE_EOF);

	generate_parser_mock(&parser, "\"\"\"bla\n\\vbla\"\"\"");
	assert_int_equal(parse_string(parser, &out), PARSE_UNEXPECTED_TEXT);

	dstr_free(&out);
	parser_mock_destroy(parser);

	UNUSED_PARAMETER(state);
}

void toml_test_parse_string(void **state)
{
	struct toml_parser *parser = NULL;
	struct dstr         out    = {0};

	generate_parser_mock(&parser, "\"bla\\nbla\"");
	assert_int_equal(parse_string(parser, &out), PARSE_SUCCESS);
	assert_int_equal(dstr_cmp(&out, "bla\nbla"), 0);

	generate_parser_mock(&parser, "\"\n\"");
	assert_int_equal(parse_string(parser, &out), PARSE_EOL);

	generate_parser_mock(&parser, "\"");
	assert_int_equal(parse_string(parser, &out), PARSE_EOF);

	generate_parser_mock(&parser, "\"bla\\vbla\"");
	assert_int_equal(parse_string(parser, &out), PARSE_UNEXPECTED_TEXT);

	dstr_free(&out);
	parser_mock_destroy(parser);

	UNUSED_PARAMETER(state);
}

void toml_test_parse_multiline_string_literal(void **state)
{
	struct toml_parser *parser = NULL;
	struct dstr         out    = {0};

	generate_parser_mock(&parser, "'''bla\n'\"\\\"bla'''");
	assert_int_equal(parse_string_literal(parser, &out), PARSE_SUCCESS);
	assert_int_equal(dstr_cmp(&out, "bla\n'\"\\\"bla"), 0);

	/* take off a ' from the end to test EOF */
	generate_parser_mock(&parser, "'''bla\nbla''");
	assert_int_equal(parse_string_literal(parser, &out), PARSE_EOF);

	dstr_free(&out);
	parser_mock_destroy(parser);

	UNUSED_PARAMETER(state);
}

void toml_test_parse_string_literal(void **state)
{
	struct toml_parser *parser = NULL;
	struct dstr         out    = {0};

	generate_parser_mock(&parser, "'bla\\nbla'");
	assert_int_equal(parse_string_literal(parser, &out), PARSE_SUCCESS);
	assert_int_equal(dstr_cmp(&out, "bla\\nbla"), 0);

	generate_parser_mock(&parser, "'\n'");
	assert_int_equal(parse_string_literal(parser, &out), PARSE_EOL);

	generate_parser_mock(&parser, "'");
	assert_int_equal(parse_string_literal(parser, &out), PARSE_EOF);

	dstr_free(&out);
	parser_mock_destroy(parser);

	UNUSED_PARAMETER(state);
}

void toml_test_parse_number(void **state)
{
	struct toml_parser *parser = NULL;
	struct toml_value   value  = {0};

	/* test floating point */
	generate_parser_mock(&parser, "-5_0.0_01e-54");
	assert_int_equal(parse_number(parser, &value), PARSE_SUCCESS);
	assert_double_equal(value.data.real, -50.001e-54, 0.001);

	generate_parser_mock(&parser, "-5_0.0_01e-54 "); /* adds space at end */
	assert_int_equal(parse_number(parser, &value), PARSE_SUCCESS);
	assert_double_equal(value.data.real, -50.001e-54, 0.001);

	generate_parser_mock(&parser, "-5_0.0_01e");
	assert_int_equal(parse_number(parser, &value), PARSE_EOF);

	generate_parser_mock(&parser, "-5_0.0_01e-");
	assert_int_equal(parse_number(parser, &value), PARSE_EOF);

	generate_parser_mock(&parser, "-5_0.0_01e- "); /* adds space at end */
	assert_int_equal(parse_number(parser, &value), PARSE_UNEXPECTED_TEXT);

	/* test typical number */
	generate_parser_mock(&parser, "-123456789");
	assert_int_equal(parse_number(parser, &value), PARSE_SUCCESS);
	assert_int_equal(value.data.integer, -123456789);

	/* test binary */
	generate_parser_mock(&parser, "0b10010010101000");
	assert_int_equal(parse_number(parser, &value), PARSE_SUCCESS);
	assert_int_equal(value.data.integer, 0b10010010101000);

	generate_parser_mock(&parser, "0b12394567");
	assert_int_equal(parse_number(parser, &value), PARSE_UNEXPECTED_TEXT);

	/* test octal */
	generate_parser_mock(&parser, "+0o1234567");
	assert_int_equal(parse_number(parser, &value), PARSE_SUCCESS);
	assert_int_equal(value.data.integer, +01234567);

	generate_parser_mock(&parser, "0o12394567");
	assert_int_equal(parse_number(parser, &value), PARSE_UNEXPECTED_TEXT);

	/* test hex */
	generate_parser_mock(&parser, "-0x6eAdBeeF bla");
	assert_int_equal(parse_number(parser, &value), PARSE_SUCCESS);
	assert_int_equal(value.data.integer, -0x6eAdBeeF);

	generate_parser_mock(&parser, "0x6ezdBeeF");
	assert_int_equal(parse_number(parser, &value), PARSE_UNEXPECTED_TEXT);

	generate_parser_mock(&parser, "");
	assert_int_equal(parse_number(parser, &value), PARSE_EOF);

	generate_parser_mock(&parser, "-");
	assert_int_equal(parse_number(parser, &value), PARSE_EOF);

	generate_parser_mock(&parser, "+inf");
	assert_int_equal(parse_number(parser, &value), PARSE_UNIMPLEMENTED);

	generate_parser_mock(&parser, "nan");
	assert_int_equal(parse_number(parser, &value), PARSE_UNIMPLEMENTED);

	toml_value_free(&value);
	parser_mock_destroy(parser);

	UNUSED_PARAMETER(state);
}

void toml_test_parse_singular_identifier(void **state)
{
	struct toml_parser *parser = NULL;
	struct dstr         out    = {0};

	generate_parser_mock(&parser, "");
	assert_int_equal(parse_singular_identifier(parser, &out, '='), PARSE_EOF);
	dstr_clear(&out);

	/* ignore string parsing branches because those are passthrough and are already tested */

	generate_parser_mock(&parser, "b*la");
	assert_int_equal(parse_singular_identifier(parser, &out, '='), PARSE_UNEXPECTED_TEXT);
	dstr_clear(&out);

	generate_parser_mock(&parser, "-Bla_5-3- bla"); /* add space at end */
	assert_int_equal(parse_singular_identifier(parser, &out, '='), PARSE_SUCCESS);
	assert_int_equal(dstr_cmp(&out, "-Bla_5-3-"), 0);
	dstr_clear(&out);

	generate_parser_mock(&parser, "-Bla_5-3="); /* parse with delimiter */
	assert_int_equal(parse_singular_identifier(parser, &out, '='), PARSE_SUCCESS);
	assert_int_equal(dstr_cmp(&out, "-Bla_5-3"), 0);
	dstr_clear(&out);

	generate_parser_mock(&parser, "test123._bla");
	assert_int_equal(parse_singular_identifier(parser, &out, '='), PARSE_SUCCESS);
	assert_int_equal(dstr_cmp(&out, "test123"), 0);
	dstr_clear(&out);

	generate_parser_mock(&parser, "b*la");
	assert_int_equal(parse_singular_identifier(parser, &out, '='), PARSE_UNEXPECTED_TEXT);
	dstr_clear(&out);

	generate_parser_mock(&parser, "bla");
	assert_int_equal(parse_singular_identifier(parser, &out, '='), PARSE_EOF);
	dstr_clear(&out);

	dstr_free(&out);
	parser_mock_destroy(parser);
}

void toml_test_parse_identifier(void **state)
{
	struct toml_parser *parser = NULL;
	struct toml_id      id     = {0};

	generate_parser_mock(&parser, "");
	assert_int_equal(parse_identifier(parser, &id, '='), PARSE_EOF);
	toml_id_free(&id);

	generate_parser_mock(&parser, "\"bla\".'bla'\n=");
	assert_int_equal(parse_identifier(parser, &id, '='), PARSE_EOL);
	toml_id_free(&id);

	generate_parser_mock(&parser, "\"bla\". ");
	assert_int_equal(parse_identifier(parser, &id, '='), PARSE_EOF);
	toml_id_free(&id);

	generate_parser_mock(&parser, "\"bla\".\n'bla'=");
	assert_int_equal(parse_identifier(parser, &id, '='), PARSE_EOL);
	toml_id_free(&id);

	generate_parser_mock(&parser, "\"bla\" bla");
	assert_int_equal(parse_identifier(parser, &id, '='), PARSE_UNEXPECTED_TEXT);
	toml_id_free(&id);

	/* ------------------------------------------------------- */
	/* test a few success cases                                */

	/* parse singule identifiers */
	generate_parser_mock(&parser, "-Bla_5-3="); /* parse with delimiter */
	assert_int_equal(parse_identifier(parser, &id, '='), PARSE_SUCCESS);
	assert_int_equal(id.path.size, 1);
	assert_int_equal(dstr_cmp(&id.path.array[0], "-Bla_5-3"), 0);
	toml_id_free(&id);

	generate_parser_mock(&parser, "-Bla_5-3 ="); /* parse with space */
	assert_int_equal(parse_identifier(parser, &id, '='), PARSE_SUCCESS);
	assert_int_equal(id.path.size, 1);
	assert_int_equal(dstr_cmp(&id.path.array[0], "-Bla_5-3"), 0);
	toml_id_free(&id);

	/* parse two identifiers */
	generate_parser_mock(&parser, "-Bla_5-3.bla_12345-="); /* parse with delimiter */
	assert_int_equal(parse_identifier(parser, &id, '='), PARSE_SUCCESS);
	assert_int_equal(id.path.size, 2);
	assert_int_equal(dstr_cmp(&id.path.array[0], "-Bla_5-3"), 0);
	assert_int_equal(dstr_cmp(&id.path.array[1], "bla_12345-"), 0);
	toml_id_free(&id);

	generate_parser_mock(&parser, "-Bla_5-3.bla_12345- ="); /* parse with space */
	assert_int_equal(parse_identifier(parser, &id, '='), PARSE_SUCCESS);
	assert_int_equal(id.path.size, 2);
	assert_int_equal(dstr_cmp(&id.path.array[0], "-Bla_5-3"), 0);
	assert_int_equal(dstr_cmp(&id.path.array[1], "bla_12345-"), 0);
	toml_id_free(&id);

	generate_parser_mock(&parser, "  -Bla_5-3 .\tbla_12345- ="); /* parse with spaces */
	assert_int_equal(parse_identifier(parser, &id, '='), PARSE_SUCCESS);
	assert_int_equal(id.path.size, 2);
	assert_int_equal(dstr_cmp(&id.path.array[0], "-Bla_5-3"), 0);
	assert_int_equal(dstr_cmp(&id.path.array[1], "bla_12345-"), 0);
	toml_id_free(&id);

	/* parse three identifiers */
	generate_parser_mock(&parser, "-Bla_5-3.bla_12345-.bla4321="); /* parse with delimiter */
	assert_int_equal(parse_identifier(parser, &id, '='), PARSE_SUCCESS);
	assert_int_equal(id.path.size, 3);
	assert_int_equal(dstr_cmp(&id.path.array[0], "-Bla_5-3"), 0);
	assert_int_equal(dstr_cmp(&id.path.array[1], "bla_12345-"), 0);
	assert_int_equal(dstr_cmp(&id.path.array[2], "bla4321"), 0);
	toml_id_free(&id);

	generate_parser_mock(&parser, "-Bla_5-3.bla_12345-.bla4321 ="); /* parse with space */
	assert_int_equal(parse_identifier(parser, &id, '='), PARSE_SUCCESS);
	assert_int_equal(id.path.size, 3);
	assert_int_equal(dstr_cmp(&id.path.array[0], "-Bla_5-3"), 0);
	assert_int_equal(dstr_cmp(&id.path.array[1], "bla_12345-"), 0);
	assert_int_equal(dstr_cmp(&id.path.array[2], "bla4321"), 0);
	toml_id_free(&id);

	generate_parser_mock(&parser, "  -Bla_5-3 .\tbla_12345- .   \tbla4321 ="); /* parse with spaces */
	assert_int_equal(parse_identifier(parser, &id, '='), PARSE_SUCCESS);
	assert_int_equal(id.path.size, 3);
	assert_int_equal(dstr_cmp(&id.path.array[0], "-Bla_5-3"), 0);
	assert_int_equal(dstr_cmp(&id.path.array[1], "bla_12345-"), 0);
	assert_int_equal(dstr_cmp(&id.path.array[2], "bla4321"), 0);
	toml_id_free(&id);

	/* ------------------------------------------------------- */
	/* test error passthrough from parse_singular_identifier() */

	generate_parser_mock(&parser, "-Bla_5-3.bla_1*345- ");
	assert_int_equal(parse_identifier(parser, &id, '='), PARSE_UNEXPECTED_TEXT);
	toml_id_free(&id);

	parser_mock_destroy(parser);
}

void toml_test_parse_value(void **state)
{
	struct toml_parser *parser = NULL;
	struct toml_value   value  = {0};

	generate_parser_mock(&parser, "");
	assert_int_equal(parse_value(parser, &value), PARSE_EOF);
	toml_value_free(&value);

	generate_parser_mock(&parser, "\n5");
	assert_int_equal(parse_value(parser, &value), PARSE_EOL);
	toml_value_free(&value);

	generate_parser_mock(&parser, "true");
	assert_int_equal(parse_value(parser, &value), PARSE_SUCCESS);
	assert_int_equal(value.type, TOML_TYPE_BOOLEAN);
	if (value.type == TOML_TYPE_BOOLEAN) {
		assert_int_equal(value.data.boolean, true);
	}
	toml_value_free(&value);

	generate_parser_mock(&parser, "false");
	assert_int_equal(parse_value(parser, &value), PARSE_SUCCESS);
	assert_int_equal(value.type, TOML_TYPE_BOOLEAN);
	if (value.type == TOML_TYPE_BOOLEAN) {
		assert_int_equal(value.data.boolean, false);
	}
	toml_value_free(&value);

	/* TODO inline arrays */
	/* TODO inline tables */

	generate_parser_mock(&parser, "\"bla\"");
	assert_int_equal(parse_value(parser, &value), PARSE_SUCCESS);
	assert_int_equal(value.type, TOML_TYPE_STRING);
	if (value.type == TOML_TYPE_STRING) {
		assert_int_equal(strcmp(value.data.string, "bla"), 0);
	}
	toml_value_free(&value);

	generate_parser_mock(&parser, "\"bla\"");
	assert_int_equal(parse_value(parser, &value), PARSE_SUCCESS);
	assert_int_equal(value.type, TOML_TYPE_STRING);
	if (value.type == TOML_TYPE_STRING) {
		assert_int_equal(strcmp(value.data.string, "bla"), 0);
	}
	toml_value_free(&value);

	generate_parser_mock(&parser, "'bla'");
	assert_int_equal(parse_value(parser, &value), PARSE_SUCCESS);
	assert_int_equal(value.type, TOML_TYPE_STRING);
	if (value.type == TOML_TYPE_STRING) {
		assert_int_equal(strcmp(value.data.string, "bla"), 0);
	}
	toml_value_free(&value);

	generate_parser_mock(&parser, "-1.2_345e-5_2");
	assert_int_equal(parse_value(parser, &value), PARSE_SUCCESS);
	assert_int_equal(value.type, TOML_TYPE_REAL);
	if (value.type == TOML_TYPE_REAL) {
		assert_double_equal(value.data.real, -1.2345e-52, 0.001);
	}
	toml_value_free(&value);

	generate_parser_mock(&parser, "-1234");
	assert_int_equal(parse_value(parser, &value), PARSE_SUCCESS);
	assert_int_equal(value.type, TOML_TYPE_INTEGER);
	if (value.type == TOML_TYPE_INTEGER) {
		assert_int_equal(value.data.integer, -1234);
	}
	toml_value_free(&value);

	generate_parser_mock(&parser, "inf");
	assert_int_equal(parse_value(parser, &value), PARSE_UNIMPLEMENTED);
	toml_value_free(&value);

	generate_parser_mock(&parser, "nan");
	assert_int_equal(parse_value(parser, &value), PARSE_UNIMPLEMENTED);
	toml_value_free(&value);

	generate_parser_mock(&parser, "1.2_345e-5_2");
	assert_int_equal(parse_value(parser, &value), PARSE_SUCCESS);
	assert_int_equal(value.type, TOML_TYPE_REAL);
	if (value.type == TOML_TYPE_REAL) {
		assert_double_equal(value.data.real, 1.2345e-52, 0.001);
	}
	toml_value_free(&value);

	generate_parser_mock(&parser, "1234");
	assert_int_equal(parse_value(parser, &value), PARSE_SUCCESS);
	assert_int_equal(value.type, TOML_TYPE_INTEGER);
	if (value.type == TOML_TYPE_INTEGER) {
		assert_int_equal(value.data.integer, 1234);
	}
	toml_value_free(&value);

	generate_parser_mock(&parser, "bla");
	assert_int_equal(parse_value(parser, &value), PARSE_UNEXPECTED_TEXT);
	toml_value_free(&value);

	parser_mock_destroy(parser);
}

static void generate_mock_id(struct toml_id *id, const char *path)
{
	struct toml_parser *parser = NULL;

	toml_id_free(id);
	generate_parser_mock(&parser, path);
	parse_identifier(parser, id, '=');
	parser_mock_destroy(parser);
}

void test_get_subtable_and_subkey(void **state)
{
	struct toml_parser *parser = NULL;
	struct toml_table *table = NULL;
	struct toml_id id = {0};
	struct dstr key = {0};

	/*
	parse_identifier(&id, "
	get_subtable_and_subkey(
	*/

	parser_mock_destroy(parser);
}

void test_parse_key_pair(void **state)
{
	struct toml_parser *parser = NULL;
	struct toml_id      id = {0};

	generate_parser_mock(&parser, "bla = \n 'bla'");
	assert_int_eq(parse_key_pair(parser, parser->root), PARSE_EOL);

	generate_parser_mock(&parser, "bla = \n 'bla'");
	assert_int_eq(parse_key_pair(parser, parser->root), PARSE_EOL);

	parser_mock_destroy(parser);
}

#endif
