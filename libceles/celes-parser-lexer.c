#include "celes-parser.h"

#include "util/lexer.h"

static void cel_token_free(struct cel_token *token)
{
	size_t i;
	for (i = 0; i < token->tokens.size; i++)
		cel_token_free(&token->tokens.array[i]);
	da_free(token->tokens);
}

void cel_parser_free(struct cel_parser *parser)
{
	size_t i;

	lexer_free(&parser->lexx);
	error_data_free(&parser->error_list);
	for (i = 0; i < parser->tokens.size; i++)
		cel_token_free(&parser->tokens.array[i]);
	da_free(parser->tokens);
	memset(parser, 0, sizeof(*parser));
}

static bool get_ident(struct lexer *lexx, struct cel_token *token)
{
	struct base_token bt = {0};

	token->type = CEL_TOKEN_IDENT;

	while (lexer_peek_token(lexx, &bt, IGNORE_WHITESPACE)) {

		if (bt.type != BASE_TOKEN_ALPHA && bt.type != BASE_TOKEN_DIGIT && *bt.text.array != '_') {
			return true;
		}

		if (!token->text.array) {
			token->text              = bt.text;
			token->row               = bt.row;
			token->col               = bt.col;
			token->passed_whitespace = bt.passed_whitespace;
		} else {
			if (bt.passed_whitespace) {
				return true;
			}
			token->text.size += bt.text.size;
		}

		lexer_get_token(lexx, NULL, IGNORE_WHITESPACE);
	}

	return false;
}

static bool get_number(struct lexer *lexx, struct cel_token *token)
{
	struct base_token bt            = {0};
	bool              found_decimal = false;

	token->type = CEL_TOKEN_NUMBER;

	while (lexer_peek_token(lexx, &bt, IGNORE_WHITESPACE)) {
		if (bt.type != BASE_TOKEN_ALPHA && bt.type != BASE_TOKEN_DIGIT && *bt.text.array != '_') {
			if (!found_decimal && *bt.text.array == '.') {
				found_decimal = true;
			} else {
				return true;
			}
		}

		if (!token->text.array) {
			token->text              = bt.text;
			token->row               = bt.row;
			token->col               = bt.col;
			token->passed_whitespace = bt.passed_whitespace;
		} else {
			if (bt.passed_whitespace) {
				return true;
			}
			token->text.size += bt.text.size;
		}

		lexer_get_token(lexx, NULL, IGNORE_WHITESPACE);
	}

	return false;
}

static bool get_token(struct lexer *lexx, struct cel_token *token);

static bool get_block(struct lexer *lexx, struct cel_token *token)
{
	struct cel_token  sub_token = {0};
	struct base_token bt        = {0};

	lexer_get_token(lexx, &bt, IGNORE_WHITESPACE);
	token->passed_whitespace = bt.passed_whitespace;
	token->text              = bt.text;
	token->row               = bt.row;
	token->col               = bt.col;
	token->type              = CEL_TOKEN_BLOCK;

	char delimiter = *bt.text.array;
	if (delimiter == '{') {
		delimiter = '}';
	} else if (delimiter == '[') {
		delimiter = ']';
	} else {
		delimiter = ')';
	}

	while (get_token(lexx, &sub_token)) {
		token->text.size = sub_token.text.array - token->text.array + sub_token.text.size;

		if (*sub_token.text.array == delimiter) {
			return true;
		}

		da_push_back(token->tokens, &sub_token);
		memset(&sub_token, 0, sizeof(sub_token));
	}

	return false;
}

static bool get_string(struct lexer *lexx, struct cel_token *token)
{
	struct base_token bt = {0};

	lexer_get_token(lexx, &bt, IGNORE_WHITESPACE);
	token->text              = bt.text;
	token->type              = CEL_TOKEN_STRING;
	token->row               = bt.row;
	token->col               = bt.col;
	token->passed_whitespace = bt.passed_whitespace;

	char delimiter = *bt.text.array;

	while (lexer_get_token(lexx, &bt, PARSE_WHITESPACE)) {
		token->text.size += bt.text.size;

		if (*bt.text.array == delimiter) {
			return true;

		} else if (*bt.text.array == '\\') {
			if (!lexer_get_token(lexx, &bt, PARSE_WHITESPACE)) {
				return false;
			}

			/* ignore potential delimiters */
			token->text.size += bt.text.size;
		}
	}

	return false;
}

static bool get_other(struct lexer *lexx, struct cel_token *token)
{
	struct base_token bt = {0};

	token->type = CEL_TOKEN_OTHER;

	if (lexer_get_token(lexx, &bt, IGNORE_WHITESPACE)) {
		token->text              = bt.text;
		token->row               = bt.row;
		token->col               = bt.col;
		token->passed_whitespace = bt.passed_whitespace;
		return true;
	}

	return false;
}

static bool parse_single_line_comment_then_get_token(struct lexer *lexx, struct cel_token *token)
{
	struct base_token bt = {0};

	/* We have already tested for and know the first two character */
	lexer_get_token(lexx, NULL, IGNORE_WHITESPACE); // '/'
	lexer_get_token(lexx, NULL, IGNORE_WHITESPACE); // '/'

	while (lexer_get_token(lexx, &bt, PARSE_WHITESPACE)) {
		if (bt.type == BASE_TOKEN_WHITESPACE && bt.ws_type == WHITESPACE_TYPE_NEWLINE) {
			return get_token(lexx, token);
		}
	}

	return false;
}

static bool parse_mutli_line_comment_recurse(struct lexer *lexx)
{
	struct base_token bt = {0};

	/* We have already tested for and know the first two character */
	lexer_get_token(lexx, NULL, IGNORE_WHITESPACE); // '/'
	lexer_get_token(lexx, NULL, IGNORE_WHITESPACE); // '*'

	while (lexer_peek_token(lexx, &bt, IGNORE_WHITESPACE)) {
		if (bt.type == BASE_TOKEN_OTHER) {
			const char *ch = bt.text.array;

			if (astrcmp_n(ch, "/*", 2) == 0) {
				if (!parse_mutli_line_comment_recurse(lexx)) {
					return false;
				}
				continue;

			} else if (astrcmp_n(ch, "*/", 2) == 0) {
				lexer_get_token(lexx, NULL, IGNORE_WHITESPACE);
				return true;
			}
		}

		lexer_get_token(lexx, NULL, IGNORE_WHITESPACE);
	}

	return false;
}

static inline bool parse_multi_line_comment_then_get_token(struct lexer *lexx, struct cel_token *token)
{
	if (parse_mutli_line_comment_recurse(lexx)) {
		return get_token(lexx, token);
	}
	return false;
}

static bool get_token(struct lexer *lexx, struct cel_token *token)
{
	struct base_token bt = {0};

	if (lexer_peek_token(lexx, &bt, IGNORE_WHITESPACE)) {
		const char *ch = bt.text.array;

		switch (bt.type) {
		case BASE_TOKEN_ALPHA:
			return get_ident(lexx, token);

		case BASE_TOKEN_DIGIT:
			return get_number(lexx, token);

		case BASE_TOKEN_OTHER:
			if (*ch == '.' && iswdigit(*(ch + 1))) {
				return get_number(lexx, token);

			} else if (*ch == '/') {
				ch++;
				if (*ch == '/') {
					return parse_single_line_comment_then_get_token(lexx, token);

				} else if (*ch == '*') {
					return parse_multi_line_comment_then_get_token(lexx, token);

				} else {
					return get_other(lexx, token);
				}

			} else if (*ch == '_') {
				return get_ident(lexx, token);

			} else if (*ch == '{' || *ch == '(' || *ch == '[') {
				return get_block(lexx, token);

			} else if (*ch == '\'' || *ch == '"') {
				return get_string(lexx, token);

			} else {
				return get_other(lexx, token);
			}
		}
	}

	return false;
}

static bool build_tree(struct cel_parser *parser, const char *file_name)
{
	struct cel_token token = {0};

	while (get_token(&parser->lexx, &token)) {
		da_push_back(parser->tokens, &token);
		memset(&token, 0, sizeof(token));
	}

	return true;
}

void cel_parser_build_tree(struct cel_parser *parser, char *file_string, size_t size, const char *file_name)
{
	lexer_start_move(&parser->lexx, file_string, size);
	build_tree(parser, file_name);
}
