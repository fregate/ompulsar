if (NOT TARGET fmt)
	add_subdirectory("${THIRDPARTY_ROOT}/parties/fmt" "${CMAKE_BINARY_DIR}/thirdparty/fmt")
endif()

if (NOT TARGET ${PACKAGE_NAME}::Fmt)
	add_library(${PACKAGE_NAME}::Fmt ALIAS fmt)
	set_target_properties(fmt PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${THIRDPARTY_ROOT}/parties/fmt/include"
		INTERFACE_LINK_LIBRARIES fmt
	)
endif()
