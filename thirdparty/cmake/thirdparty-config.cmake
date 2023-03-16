set(PACKAGE_NAME thirdparty)
get_filename_component(THIRDPARTY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(_components
	fmt
	libbert
	pulsar-client
)

foreach(_comp ${thirdparty_FIND_COMPONENTS})
	if (NOT _comp IN_LIST _components)
		set(${PACKAGE_NAME}::${_comp}_FOUND false)
		set(${PACKAGE_NAME}::${_comp}_NOT_FOUND_MESSAGE "Unknown thirdparty component: thirdparty::${_comp}")
	else()
		include(${CMAKE_CURRENT_LIST_DIR}/${_comp}.cmake)
	endif()
endforeach()
