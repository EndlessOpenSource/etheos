# $Id$
# EOSERV is released under the zlib license.
# See LICENSE.txt for more info.

cmake_minimum_required(VERSION 2.6)
project(eoserv C CXX)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

cmake_policy(SET CMP0054 NEW)

# ---------
#  Options
# ---------

option(EOSERV_WANT_MYSQL "Enables MariaDB/MySQL server database support." ON)
option(EOSERV_WANT_SQLITE "Enables SQLite3 embedded database support." ON)
option(EOSERV_WANT_SQLSERVER "Enables Microsoft SQL Server database support." OFF)

option(EOSERV_USE_PRECOMPILED_HEADERS "Uses a precompiled header to speed up compilation." ON)
option(EOSERV_USE_UNITY_BUILD "Compiles multiple source files in one translation unit to speed up compilation." ON)
option(EOSERV_USE_CLANG_MODULES "Uses Clang modules (if available) to speed up compilation. Note: Clang modules are an experimental feature." OFF)

option(EOSERV_GEN_PRECOMPILED_HEADERS "Generated precompiled header automatically. Requires a shell with basic binutils including grep and awk." OFF)
option(EOSERV_NO_DATA "Disables copying of data files in to build directory" OFF)

option(EOSERV_DEBUG_QUERIES "Enables printing of database queries to debug output" OFF)

# --------------
#  Source files
# --------------

include(SourceFileList)

# Platform-specific source files
# Unity builds handle their own platform checking for now
if(WIN32)
	list(APPEND eoserv_ALL_SOURCE_FILES ${eoserv_WIN32_SOURCE_FILES})
endif()

if(EOSERV_USE_UNITY_BUILD)
	set(eoserv_SOURCE_FILES ${eoserv_UNITY_SOURCE_FILES})
	set(eoserv_MAIN_FILES ${eoserv_UNITY_HANDLER_FILES} tu/main.cpp)
else()
	set(eoserv_SOURCE_FILES ${eoserv_ALL_SOURCE_FILES})
	set(eoserv_MAIN_FILES ${eoserv_ALL_HANDLER_FILES} src/main.cpp)
endif()

# Fancy icon on Windows
if(WIN32)
	list(APPEND eoserv_MAIN_FILES "project/winres.rc")
	list(APPEND eoserv_MAIN_FILES ${eoserv_WIN32_SOURCE_FILES_MAIN})

	# Link directly to SQLite on Windows
	if(EOSERV_WANT_SQLITE)
		list(APPEND eoserv_SOURCE_FILES ${eoserv_SQLITE_SOURCE_FILES})
	endif()
endif()

set(eoserv_LIBRARIES)

# ----------------
#  Compiler flags
# ----------------

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set(eoserv_GCC TRUE)
	set(eoserv_COMPILER_SUPPORTED TRUE)

	#if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "5")
	#	set(eoserv_COMPILER_SUPPORTED FALSE)
	#endif()

	# EOSERV will try to support GCC 4.8 and 4.9 until GCC 5 becomes standard in MinGW distributions
	if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "4.8")
		set(eoserv_COMPILER_SUPPORTED FALSE)
	endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	set(eoserv_CLANG TRUE)
	set(eoserv_COMPILER_SUPPORTED TRUE)

	if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "3.4")
		set(eoserv_COMPILER_SUPPORTED FALSE)
	endif()
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	set(eoserv_MSVC TRUE)
	set(eoserv_COMPILER_SUPPORTED TRUE)

	# Fix warnings due to not using the _s version of a lot of different functions on MSVC
	#
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

	if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS "14.0")
		set(eoserv_COMPILER_SUPPORTED FALSE)
	endif()

	add_compile_definitions(
		$<$<CONFIG:Debug>:DEBUG>
		$<$<CONFIG:Release>:RELEASE>
	)
endif()

if((eoserv_GCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "5.0")
OR (eoserv_CLANG AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "3.5"))
	set(eoserv_CXX1Y TRUE)
endif()

if(eoserv_GCC OR eoserv_CLANG)
	if(NOT " ${CMAKE_CXX_FLAGS} " MATCHES " -std=")
		if(eoserv_CXX1Y)
			set(eoserv_STDFLAG "-std=c++1y")
		else()
			set(eoserv_STDFLAG "-std=c++14")
		endif()
	endif()
endif()

if(eoserv_STDFLAG)
	if(NOT " ${CMAKE_CXX_FLAGS} " MATCHES " -std=")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${eoserv_STDFLAG}")
	endif()
endif()

# These non-standard anti-optimizations are currently required for a correctly functioning server
if(eoserv_GCC OR eoserv_CLANG)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fwrapv -fno-strict-aliasing -Wall -Wextra")
endif()

if(NOT eoserv_COMPILER_SUPPORTED)
	message(WARNING "You are using an unsupported compiler, the build may fail or result in an unstable/exploitable server. Detected compiler was ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}. EOSERV is only tested on GCC 5+ and Clang 3.4+. Visual C++ 2015/14.0+ may work but is untested.")
endif()

if(EOSERV_USE_CLANG_MODULES)
	if(NOT eoserv_CLANG)
		message(WARNING "Not using Clang, modules unavailable. Detected compiler was ${CMAKE_CXX_COMPILER_ID}.")
	else()
		# TODO: Could use a check to see if libc++ is the default standard library
		if (NOT " ${CMAKE_CXX_FLAGS} " MATCHES " -stdlib=libc\\+\\+ ")
			message(WARNING "Using libc++ is highly reccommended when using Clang modules. Add -stdlib=libc++ to CMAKE_CXX_FLAGS.")
		endif()

		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fmodules")
		add_compile_definitions(CLANG_MODULES_WORKAROUND)
	endif()
endif()

if(EOSERV_DEBUG_QUERIES)
	add_compile_definitions(DATABASE_DEBUG)
endif()

string(TOLOWER "${CMAKE_BUILD_TYPE}" BuildType)

if(BuildType STREQUAL "debug")
	add_compile_definitions(DEBUG)

	# Disable optimizations for debug builds with GCC.
	IF(CMAKE_COMPILER_IS_GNUCC)
	    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")
	    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0")
	ENDIF(CMAKE_COMPILER_IS_GNUCC)
endif()

# ------------
#  Test files
# ------------

include(DownloadGoogleTest)

# ---------
#  Outputs
# ---------

add_library(eoserv_lib STATIC ${eoserv_SOURCE_FILES})
add_executable(eoserv ${eoserv_MAIN_FILES})
add_executable(eoserv_test ${TestFiles})

target_include_directories(eoserv_test PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_include_directories(eoserv_test PUBLIC ${CMAKE_BINARY_DIR}/googletest-src/googlemock/include)
target_link_libraries(eoserv_test gtest_main gmock eoserv_lib)

if(EOSERV_USE_PRECOMPILED_HEADERS)
	add_dependencies(eoserv_test eoserv-pch)
endif()

add_test(NAME eoserv_test COMMAND eoserv_test)

set (CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install)

# -----------
#  Libraries
# -----------

if(EOSERV_WANT_MYSQL)
	find_package(MariaDB REQUIRED)
	include_directories(${MARIADB_INCLUDE_DIR})
	list(APPEND eoserv_LIBRARIES ${MARIADB_LIBRARY})
	add_compile_definitions(DATABASE_MYSQL)
endif()

if(EOSERV_WANT_SQLITE)
	if (WIN32)
		# On Windows, SQLite is directly compiled into the executable (per SQLite recommendation)
		include_directories(${CMAKE_SOURCE_DIR}/sqlite/include)
	else()
		find_package(SQLite3 REQUIRED)
		include_directories(${SQLITE3_INCLUDE_DIR})
		list(APPEND eoserv_LIBRARIES ${SQLITE3_LIBRARY})
	endif()

	add_compile_definitions(DATABASE_SQLITE)
endif()

if(EOSERV_WANT_SQLSERVER)
	find_package(ODBC REQUIRED)
	include_directories(${ODBC_INCLUDE_DIRS})
	list(APPEND eoserv_LIBRARIES ${ODBC_LIBRARIES})
	add_compile_definitions(DATABASE_SQLSERVER)
endif()

if (NOT EOSERV_WANT_MYSQL AND NOT EOSERV_WANT_SQLITE AND NOT EOSERV_WANT_SQLSERVER)
	message(FATAL_ERROR "Either MySQL, SQLite, or SQL Server support must be enabled.")
endif()

if(WIN32)
	list(APPEND eoserv_LIBRARIES winmm ws2_32)

	if (EOSERV_WANT_MYSQL)
		list(APPEND eoserv_LIBRARIES shlwapi crypt32 secur32)
	endif()
else()
	list(APPEND eoserv_LIBRARIES pthread)
endif()

include(DownloadBcrypt)

target_include_directories(eoserv PUBLIC ${CMAKE_BINARY_DIR}/bcrypt-src/include)
target_include_directories(eoserv_lib PUBLIC ${CMAKE_BINARY_DIR}/bcrypt-src/include)
list(APPEND eoserv_LIBRARIES bcrypt)

target_link_libraries(eoserv eoserv_lib)
target_link_libraries(eoserv_lib ${eoserv_LIBRARIES})

install(TARGETS eoserv RUNTIME DESTINATION .)
install(TARGETS eoserv_test RUNTIME DESTINATION ./test)

foreach (File ${ExtraFiles})
	get_filename_component(Dir "${File}" DIRECTORY)

	if(Dir STREQUAL "")
		set(Dir ".")
	endif()

	install(FILES ${File} DESTINATION ${Dir})
endforeach()

install(DIRECTORY ${LocalConf} DESTINATION ${LocalConf} FILES_MATCHING PATTERN "*.ini")

# ---------------------
#  Precompiled Headers
# ---------------------

include(EOSERV_PCH)

if(EOSERV_USE_PRECOMPILED_HEADERS)
	if(EOSERV_GEN_PRECOMPILED_HEADERS)
		add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/eoserv-pch.h
			COMMAND ${CMAKE_SOURCE_DIR}/autogen-pch.sh ${eoserv_ALL_SOURCE_FILES} > ${CMAKE_BINARY_DIR}/eoserv-pch.h
			MAIN_DEPENDENCY autogen-pch.sh
			# This dependency is commented out to avoid constant rebuilding on any source file change
			# DEPENDS autogen-pch.sh ${eoserv_ALL_SOURCE_FILES}
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		)
	else()
		add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/eoserv-pch.h
			COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/src/stdafx.h ${CMAKE_BINARY_DIR}/eoserv-pch.h
			MAIN_DEPENDENCY src/stdafx.h
			DEPENDS src/stdafx.h
		)
	endif()

	add_custom_target(eoserv-pch-autogen
		DEPENDS ${CMAKE_BINARY_DIR}/eoserv-pch.h
	)

	compile_pch(eoserv-pch ${CMAKE_BINARY_DIR}/eoserv-pch.h)

	add_dependencies(eoserv-pch-autogen eoserv-pch)
	add_dependencies(eoserv eoserv-pch)
	add_dependencies(eoserv_lib eoserv-pch)

	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${eoserv-pch_INCLUDE_FLAG}")
endif()

# ------------
#  Data files
# ------------

if (NOT EOSERV_NO_DATA)
	foreach (File ${ExtraFiles})
		configure_file(${File} ${CMAKE_CURRENT_BINARY_DIR}/${File} COPYONLY)
	endforeach()
endif()
