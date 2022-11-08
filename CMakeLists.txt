cmake_minimum_required(VERSION 3.7 FATAL_ERROR)
set(CMAKE_CXX_STANDARD 17)
project(libmp-example)

# Main executables
add_executable(main main.cpp)
set_property(TARGET main PROPERTY CXX_STANDARD 17)
set_property(TARGET main PROPERTY CXX_STANDARD_REQUIRED ON)

# Link MediaPipe Shared Lib
set(MEDIAPIPE_DIR "C:/Users/rk274/code/mediapipe")
if(NOT EXISTS "${MEDIAPIPE_DIR}")
	message(FATAL_ERROR "Mediapipe directory '${MEDIAPIPE_DIR}' not found!")
endif()
set(LIBMP_TARGET_NAME "libmp")
set(LIBMP_SOURCE_DIR "${MEDIAPIPE_DIR}/mediapipe/examples/desktop/${LIBMP_TARGET_NAME}")
set(LIBMP_BIN_DIR "${MEDIAPIPE_DIR}/bazel-bin/mediapipe/examples/desktop/${LIBMP_TARGET_NAME}")
add_library(LIBMP SHARED IMPORTED)
IF(WIN32)
	set(LIBMP_SHARED_LIBRARY "${LIBMP_BIN_DIR}/${LIBMP_TARGET_NAME}.dll")
	set(LIBMP_IMPORT_LIB "${LIBMP_BIN_DIR}/${LIBMP_TARGET_NAME}.dll.if.lib")
	set_target_properties(LIBMP PROPERTIES
    	IMPORTED_LOCATION "${LIBMP_SHARED_LIBRARY}"
		IMPORTED_IMPLIB "${LIBMP_IMPORT_LIB}"
    	INTERFACE_INCLUDE_DIRECTORIES "${LIBMP_SOURCE_DIR}"
	)
ELSEIF(UNIX)
	set(LIBMP_SHARED_LIBRARY "${LIBMP_BIN_DIR}/${LIBMP_TARGET_NAME}.so")
	set_target_properties(LIBMP PROPERTIES
    	IMPORTED_LOCATION "${LIBMP_SHARED_LIBRARY}"
    	INTERFACE_INCLUDE_DIRECTORIES "${LIBMP_SOURCE_DIR}"
	)
ENDIF()
target_link_libraries(main LIBMP)

# Protobuf
# may have to set protobuf_source_dir manually if bazel's workspace output dir is atypical
get_filename_component(MEDIAPIPE_DIRNAME "${MEDIAPIPE_DIR}" NAME)
set(protobuf_source_dir "${MEDIAPIPE_DIR}/bazel-${MEDIAPIPE_DIRNAME}/external/com_google_protobuf")
include("${CMAKE_CURRENT_SOURCE_DIR}/libprotobuf.cmake")

# MediaPipe Formats
file(GLOB_RECURSE MP_FORMATS_SRC "${MEDIAPIPE_DIR}/bazel-bin/mediapipe/framework/formats/*.cc")
if(MSVC)
  # MSVC warning suppressions
  add_definitions(${MSVC_WARNINGS})
endif(MSVC)
add_library(MP_FORMATS STATIC ${MP_FORMATS_SRC})
target_include_directories(MP_FORMATS PUBLIC "${MEDIAPIPE_DIR}/bazel-bin")
target_link_libraries(MP_FORMATS libprotobuf)
target_link_libraries(main MP_FORMATS)

### Post-build copies ###
# LibMP - MediaPipe Shared Library
add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${LIBMP_SHARED_LIBRARY}" "$<TARGET_FILE_DIR:main>"
    COMMENT "Copying ${LIBMP_SHARED_LIBRARY}"
)
# Mediapipe Modules
add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${MEDIAPIPE_DIR}/bazel-bin/mediapipe/modules" "$<TARGET_FILE_DIR:main>/mediapipe/modules"
    COMMENT "Copying mediapipe/modules"
)
# Mediapipe Models
add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${MEDIAPIPE_DIR}/bazel-bin/mediapipe/models" "$<TARGET_FILE_DIR:main>/mediapipe/models"
    COMMENT "Copying mediapipe/models"
)
# OpenCV (Windows only)
IF(WIN32)
	add_custom_command(TARGET main POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy "${LIBMP_BIN_DIR}/opencv_world460.dll" "$<TARGET_FILE_DIR:main>"
		COMMENT "Copying OpenCV DLL"
	)
ENDIF()
# Input image
add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/input.jpg" "$<TARGET_FILE_DIR:main>/input.jpg"
    COMMENT "Copying input JPG"
)
