project(celes)

add_executable(celes)
target_link_libraries(celes
	PRIVATE
		libceles
)
target_sources(celes
	PRIVATE
		main.c
)
