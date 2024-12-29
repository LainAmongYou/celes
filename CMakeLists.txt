cmake_minimum_required(VERSION 3.22...3.25)

project(celes)

option(ENABLE_TESTS "Enable tests" OFF)

# set(CMAKE_C_STANDARD 90)
if(MSVC)
	add_compile_options(/WX /WX)
else()
	add_compile_options(-Wall -Wextra)
	add_compile_options(-Wno-unused-variable -Wno-unused-parameter -Wno-switch -Wunused-function)
endif()

add_subdirectory(libceles)
add_subdirectory(celes)

if(ENABLE_TESTS)
	add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND}
	     --force-new-ctest-process
	     --output-on-failure
	)
	enable_testing()
	add_subdirectory(tests)
endif()
