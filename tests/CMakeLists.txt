project(tests)

add_executable(test-toml)
target_sources(test-toml PRIVATE test-toml.c)
target_link_libraries(test-toml libceles)

add_test(test-toml ${CMAKE_CURRENT_BINARY_DIR}/test-toml)
