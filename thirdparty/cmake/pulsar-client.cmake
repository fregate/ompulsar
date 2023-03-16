if (NOT TARGET pulsarStatic)
	find_package(Boost REQUIRED)
	find_package(OpenSSL REQUIRED)
	find_package(Protobuf REQUIRED)

	set(BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
	set(BUILD_DYNAMIC_LIB OFF CACHE BOOL "" FORCE)
	set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
	add_subdirectory("${THIRDPARTY_ROOT}/parties/pulsar-client" "${CMAKE_BINARY_DIR}/thirdparty/pulsar-client")
endif()

if (NOT TARGET ${PACKAGE_NAME}::PulsarClient)
	add_library(${PACKAGE_NAME}::pulsarStatic IMPORTED INTERFACE)
	add_library(${PACKAGE_NAME}::PulsarClient ALIAS ${PACKAGE_NAME}::pulsarStatic)
	set_target_properties(${PACKAGE_NAME}::pulsarStatic PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${THIRDPARTY_ROOT}/parties/pulsar-client/include"
		INTERFACE_LINK_LIBRARIES pulsarStatic
	)
endif()
