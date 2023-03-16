if (NOT TARGET ${PACKAGE_NAME}::HashMap)
	add_library(thrdpartyHashMap STATIC
		${THIRDPARTY_ROOT}/parties/hashmap.c/hashmap.c
		${THIRDPARTY_ROOT}/parties/hashmap.c/hashmap.h
	)

	target_compile_options(thrdpartyHashMap
		PUBLIC -fPIC
	)

	# set_target_properties(thrdpartyHashMap PROPERTIES
	# 	INTERFACE_INCLUDE_DIRECTORIES "${THIRDPARTY_ROOT}/parties/hashmap.c"
	# 	INTERFACE_LINK_LIBRARIES thrdpartyHashMap
	# )

	target_include_directories(thrdpartyHashMap
	PUBLIC
		${THIRDPARTY_ROOT}/parties/hashmap.c/
	)

	add_library(${PACKAGE_NAME}::HashMap ALIAS thrdpartyHashMap)
endif()
