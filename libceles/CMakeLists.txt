project(libceles)

add_library(libceles STATIC)
target_sources(libceles
	PRIVATE
		celes-parser-lexer.c
		celes-parser.c
		util/toml.c
		util/hash.c
		util/dstr.c
		util/platform.c
		util/platform-nix.c
		util/lexer.c
		util/utf8.c
	PUBLIC
		celes-parser.h
		util/toml.h
		util/hash.h
		util/bmem.h
		util/darray.h
		util/dstr.h
		util/platform.h
		util/lexer.h
		util/utf8.h
		util/util-defs.h
)
target_include_directories(libceles
	PUBLIC
		"${CMAKE_CURRENT_SOURCE_DIR}/"
)

if(ENABLE_TESTS)
	find_package(CMocka CONFIG REQUIRED)
	target_link_libraries(libceles cmocka::cmocka)
	target_compile_definitions(libceles PRIVATE ENABLE_TESTS)
endif()
