#include <stdio.h>
#include <celes-parser.h>
#include <util/hash.h>
#include <util/platform.h>
#include <util/toml.h>

static bool build(int argc, char *argv[])
{
	toml_t *config;
	char   *file_string;
	char   *errors = NULL;
	size_t  size;
	int     err;

	/*file_string = os_quick_read_utf8_file("../../other/test/font.celes", &size);
	if (file_string) {
		struct cel_parser parser = {0};
		cel_parser_build_tree(&parser, file_string, size, "some dumb file");
		cel_parser_free(&parser);
	}*/

	err = toml_open(&config, "Project.toml", &errors);
	if (err == TOML_FILE_NOT_FOUND) {
		printf("Could not find file dingus\n");
		return false;
	} else if (err != TOML_SUCCESS) {
		if (errors) {
			printf("Error parsing file:\n%s\n", errors);
			bfree(errors);
		}
		return false;
	}

	const char *name = toml_get_string(config, "Build", "Name");
	if (!name) {
		printf("No program name specified\n");
		return false;
	}

	// find like, program.celes or something. I donno.

	return true;
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		printf("Celes transpiler\n\nUse: celes [command]\n\nCommands:\n\tbuild   build stuff\n");
		return 0;
	}

	if (astrcmpi(argv[1], "build") == 0) {
		return build(argc, argv) ? 0 : -1;
	}

	printf("You appear to have entered something that celes does not appear to understand. "
	       "This must be corrected. *eyes glow red*\n");
	return -1;
}
