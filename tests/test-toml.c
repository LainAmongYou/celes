#include <stddef.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

extern void toml_test_eol(void **state);
extern void toml_test_parse_escape_code(void **state);
extern void toml_test_expect_next_char(void **state);
extern void toml_test_parse_multiline_string(void **state);
extern void toml_test_parse_string(void **state);
extern void toml_test_parse_multiline_string_literal(void **state);
extern void toml_test_parse_string_literal(void **state);
extern void toml_test_parse_number(void **state);
extern void toml_test_parse_singular_identifier(void **state);
extern void toml_test_parse_identifier(void **state);
extern void toml_test_parse_value(void **state);

int main()
{
	const struct CMUnitTest tests[] = {
	        cmocka_unit_test(toml_test_eol),
	        cmocka_unit_test(toml_test_parse_escape_code),
	        cmocka_unit_test(toml_test_expect_next_char),
		cmocka_unit_test(toml_test_parse_multiline_string),
		cmocka_unit_test(toml_test_parse_string),
		cmocka_unit_test(toml_test_parse_multiline_string_literal),
		cmocka_unit_test(toml_test_parse_string_literal),
		cmocka_unit_test(toml_test_parse_number),
		cmocka_unit_test(toml_test_parse_singular_identifier),
		cmocka_unit_test(toml_test_parse_identifier),
		cmocka_unit_test(toml_test_parse_value),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
