#pragma once

#include "util/lexer.h"
#include "util/darray.h"

#ifdef __cplusplus
extern "C" {
#endif

enum cel_token_type {
	CEL_TOKEN_NONE,
	CEL_TOKEN_IDENT,
	CEL_TOKEN_NUMBER,
	CEL_TOKEN_STRING,
	CEL_TOKEN_BLOCK,
	CEL_TOKEN_OTHER
};

struct cel_token {
	enum cel_token_type type;
	struct strref       text;
	uint32_t            row;
	uint32_t            col;
	bool                passed_whitespace;

	DARRAY(struct cel_token) tokens;
};

struct cel_parser {
	struct lexer      lexx;
	struct error_data error_list;

	DARRAY(struct cel_token) tokens;
};

EXPORT void cel_parser_free(struct cel_parser *parser);
EXPORT void cel_parser_build_tree(struct cel_parser *parser, char *file_string, size_t size, const char *file_name);

#ifdef __cplusplus
}
#endif
