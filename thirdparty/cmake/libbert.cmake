if (NOT TARGET libbert)
	add_subdirectory("${THIRDPARTY_ROOT}/parties/libbert" "${CMAKE_BINARY_DIR}/thirdparty/libbert")
endif()

if (NOT TARGET ${PACKAGE_NAME}::Bert)
	add_library(${PACKAGE_NAME}::Bert ALIAS bert)
	set_target_properties(bert PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${THIRDPARTY_ROOT}/parties/libbert/include"
		INTERFACE_LINK_LIBRARIES bert
	)
endif()
