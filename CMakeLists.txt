# option to set custom Grok name space
#set(GROK_NAMESPACE "FOO")
cmake_minimum_required(VERSION 3.12)

if(NOT GROK_NAMESPACE)
  set(GROK_NAMESPACE "GROK")
endif()
set(GROK_LIBRARY_NAME grokj2k)
set(GROK_PLUGIN_NAME grokj2k_plugin)

project(${GROK_NAMESPACE} )

# Do full dependency headers.
include_regular_expression("^.*$")

#-----------------------------------------------------------------------------
# GROK version number, useful for packaging and doxygen doc:
set(GROK_VERSION_MAJOR 8)
set(GROK_VERSION_MINOR 0)
set(GROK_VERSION_BUILD 2)
set(GROK_VERSION
  "${GROK_VERSION_MAJOR}.${GROK_VERSION_MINOR}.${GROK_VERSION_BUILD}")
set(PACKAGE_VERSION
  "${GROK_VERSION_MAJOR}.${GROK_VERSION_MINOR}.${GROK_VERSION_BUILD}")

# Because autotools does not support X.Y notation for SOVERSION, we have to use
# two numbering, one for the Grok version and one for Grok soversion
# version | soversion
#   7.0.0 |  1
#   7.1.0 |  1
#   7.2.0 |  1
#   7.3.0 |  1
#   7.4.0 |  1
#   7.5.0 |  1
#   7.6.0 |  1
#   7.6.1 |  1
#   7.6.2 |  1
#   7.6.3 |  1
#   7.6.4 |  1
#   7.6.5 |  1
#   7.6.6 |  1
#   8.0.0 |  1
#   8.0.1 |  1
#   8.0.2 |  1
if(NOT GROK_SOVERSION)
  set(GROK_SOVERSION 1)
endif(NOT GROK_SOVERSION)
set(GROK_LIBRARY_PROPERTIES
  VERSION   "${GROK_VERSION_MAJOR}.${GROK_VERSION_MINOR}.${GROK_VERSION_BUILD}"
  SOVERSION "${GROK_SOVERSION}"
)

# --------------------------------------------------------------------------
# Path to additional CMake modules
set(CMAKE_MODULE_PATH
    ${${GROK_NAMESPACE}_SOURCE_DIR}/cmake
    ${CMAKE_MODULE_PATH})

if(WIN32)
  if(NOT BORLAND)
    if(NOT CYGWIN)
      if(NOT MINGW)
        if(NOT ITK_ENABLE_VISUAL_STUDIO_DEPRECATED_C_WARNINGS)
          add_definitions(
            -D_CRT_FAR_MAPPINGS_NO_DEPRECATE
            -D_CRT_IS_WCTYPE_NO_DEPRECATE
            -D_CRT_MANAGED_FP_NO_DEPRECATE
            -D_CRT_NONSTDC_NO_DEPRECATE
            -D_CRT_SECURE_NO_DEPRECATE
            -D_CRT_SECURE_NO_DEPRECATE_GLOBALS
            -D_CRT_SETERRORMODE_BEEP_SLEEP_NO_DEPRECATE
            -D_CRT_TIME_FUNCTIONS_NO_DEPRECATE
            -D_CRT_VCCLRIT_NO_DEPRECATE
            -D_SCL_SECURE_NO_DEPRECATE
            )
        endif()
      endif()
    endif()
  endif()
endif()


# --------------------------------------------------------------------------
# Install directories
include(GNUInstallDirs)
# Build DOCUMENTATION (not in ALL target and only if Doxygen is found)
option(BUILD_DOC "Build the HTML documentation (with doxygen if available)." OFF)

string(TOLOWER ${PROJECT_NAME} projectname)
set(GROK_INSTALL_SUBDIR "grok-${GROK_VERSION_MAJOR}.${GROK_VERSION_MINOR}")

if(NOT GROK_INSTALL_PACKAGE_DIR)
  set(GROK_INSTALL_PACKAGE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/${GROK_INSTALL_SUBDIR}")
endif()

if (APPLE)
	list(APPEND GROK_LIBRARY_PROPERTIES INSTALL_NAME_DIR "${CMAKE_INSTALL_FULL_LIBDIR}")
	option(GROK_USE_DSYMUTIL "Call dsymutil on binaries after build." OFF)
endif()

#-----------------------------------------------------------------------------
# Big endian test:
include (${CMAKE_ROOT}/Modules/TestBigEndian.cmake)
TEST_BIG_ENDIAN(GROK_BIG_ENDIAN)

#-----------------------------------------------------------------------------
# Set up file for setting custom ctest vars
configure_file(
  ${${GROK_NAMESPACE}_SOURCE_DIR}/cmake/CTestCustom.cmake.in
  ${${GROK_NAMESPACE}_BINARY_DIR}/CTestCustom.cmake
  @ONLY
  )

#-----------------------------------------------------------------------------
# Grok build configuration options.
option(BUILD_SHARED_LIBS "Build Grok shared library and link executables against it." ON)
option(BUILD_STATIC_LIBS "Build Grok static library." ON)
set (EXECUTABLE_OUTPUT_PATH ${GROK_BINARY_DIR}/bin CACHE PATH "Single output directory for building all executables.")
set (LIBRARY_OUTPUT_PATH ${GROK_BINARY_DIR}/bin CACHE PATH "Single output directory for building all libraries.")
mark_as_advanced(LIBRARY_OUTPUT_PATH EXECUTABLE_OUTPUT_PATH)

#-----------------------------------------------------------------------------
# Compiler specific flags:
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  set(GROK_COMPILE_OPTIONS ${GROK_COMPILE_OPTIONS} -Wall -Wextra -Wconversion -Wsign-conversion -Wunused-parameter)
endif()

IF(NOT ${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")
	find_package(SSE)
ENDIF(NOT ${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")

#-----------------------------------------------------------------------------
# grk_config.h generation (1/2)

# Option choose whether to use static runtime
include(ucm)
if(BUILD_SHARED_LIBS)
    ucm_set_runtime(DYNAMIC)
else()
    ucm_set_runtime(STATIC)
endif()

# Check if some include files are provided by the system
include(EnsureFileInclude)
# These files are mandatory
ensure_file_include("string.h"   HAVE_STRING_H YES)
ensure_file_include("memory.h"   HAVE_MEMORY_H YES)
ensure_file_include("stdlib.h"   HAVE_STDLIB_H YES)
ensure_file_include("stdio.h"    HAVE_STDIO_H  YES)
ensure_file_include("math.h"     HAVE_MATH_H   YES)
ensure_file_include("float.h"    HAVE_FLOAT_H  YES)
ensure_file_include("time.h"     HAVE_TIME_H   YES)
ensure_file_include("stdarg.h"   HAVE_STDARG_H YES)
ensure_file_include("ctype.h"    HAVE_CTYPE_H  YES)
ensure_file_include("assert.h"   HAVE_ASSERT_H YES)

CHECK_INCLUDE_FILE("strings.h"      HAVE_STRINGS_H)
CHECK_INCLUDE_FILE("sys/stat.h"     HAVE_SYS_STAT_H)
CHECK_INCLUDE_FILE("sys/types.h"    HAVE_SYS_TYPES_H)
CHECK_INCLUDE_FILE("unistd.h"       HAVE_UNISTD_H)

# Enable Large file support
include(TestLargeFiles)
GROK_TEST_LARGE_FILES(GROK_HAVE_LARGEFILES)

# Allocating Aligned Memory Blocks
include(CheckIncludeFiles)
check_include_files(malloc.h GROK_HAVE_MALLOC_H)
include(CheckSymbolExists)
# _aligned_alloc https://msdn.microsoft.com/en-us/library/8z34s9c6.aspx
check_symbol_exists(_aligned_malloc malloc.h GROK_HAVE__ALIGNED_MALLOC)
check_symbol_exists(aligned_alloc stdlib.h GROK_HAVE_ALIGNED_ALLOC)
# posix_memalign (needs _POSIX_C_SOURCE >= 200112L on Linux)
set(CMAKE_REQUIRED_DEFINITIONS -D_POSIX_C_SOURCE=200112L)
check_symbol_exists(posix_memalign stdlib.h GROK_HAVE_POSIX_MEMALIGN)
unset(CMAKE_REQUIRED_DEFINITIONS)
# memalign (obsolete)
check_symbol_exists(memalign malloc.h GROK_HAVE_MEMALIGN)
#-----------------------------------------------------------------------------
# Build Library
add_subdirectory(src/lib)
option(BUILD_LUTS_GENERATOR "Build utility to generate t1_luts.h" OFF)

#-----------------------------------------------------------------------------
# Build Applications
option(BUILD_CODEC "Build the CODEC executables" ON)
option(BUILD_PLUGIN_LOADER "Enable loading of T1 plugin" OFF)
mark_as_advanced(BUILD_PLUGIN_LOADER)
option(URING OFF "Enable support for io_uring (requires liburing and Linux kernel >= 5.8)")
mark_as_advanced(URING)

#---Check for liburing----------------------------------------------------------------
if (URING)
  if(NOT CMAKE_SYSTEM_NAME MATCHES Linux)
    set(URING OFF CACHE BOOL "Disabled because liburing is only available on Linux" FORCE)
    message(STATUS "liburing was disabled : only available on Linux")
    set(GROK_HAVE_URING undef)
  else()
    message(STATUS "Looking for liburing")
    find_package(liburing)
    if(NOT LIBURING_FOUND)
      if(fail-on-missing)
        message(FATAL_ERROR "liburing not found and uring option required")
      else()
        message(STATUS "liburing not found. Switching off uring option")
        set(uring OFF CACHE BOOL "Disabled because liburing was not found (${uring_description})" FORCE)
      endif()
    else()
    	message(STATUS "Found liburing") 
    	set(GROK_HAVE_URING define)     
    endif()
  endif()
endif()

find_package(PerlLibs)
if (PERLLIBS_FOUND)
 	message(STATUS "Perl libraries found")
 	execute_process(COMMAND ${PERL_EXECUTABLE} -MImage::ExifTool -e ""
     ERROR_QUIET RESULT_VARIABLE status)
     if (NOT status)
      	message(STATUS "ExifTool Perl module found")
       	set(GROK_HAVE_EXIFTOOL define)  
     else()
      	message(STATUS "ExifTool Perl module not found")     
     endif()
endif(PERLLIBS_FOUND)


if(BUILD_CODEC)
  # OFF: 3rd party libs will only be built if not found in system
  # ON:  3rd party libs will always be built
  option(BUILD_THIRDPARTY "Build the thirdparty executables if it is needed" OFF)
  # OFF: libjpeg library wil not be used to support JPEG file format
  # ON:  libjpeg library will be used to support JPEG file format,
  #      if found in system
  option(GRK_USE_LIBJPEG "Use libjpeg library to support JPEG file format, if present in system" OFF)
  add_subdirectory(thirdparty)
  add_subdirectory(src/bin)
endif ()

#-----------------------------------------------------------------------------
# grk_config.h generation (2/2)
configure_file(
 ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/jp2/grk_config.h.cmake.in
 ${CMAKE_CURRENT_BINARY_DIR}/src/lib/jp2/grk_config.h
 @ONLY
 )

 configure_file(
 ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/jp2/grk_config_private.h.cmake.in
 ${CMAKE_CURRENT_BINARY_DIR}/src/lib/jp2/grk_config_private.h
 @ONLY
 )

#-----------------------------------------------------------------------------
# build documentation in doc subdir:
if(BUILD_DOC)
  add_subdirectory(doc)
endif()

#-----------------------------------------------------------------------------
# Build Testing
option(BUILD_TESTING "Build the tests." OFF)
if(BUILD_TESTING)
  if(BUILD_CODEC)
    enable_testing()
    include(CTest)

    # Search for Grok test data
    # The test data can be cloned from https://github.com/GrokImageCompression/grok-test-data
    find_path(GRK_DATA_ROOT README-GROK-TEST-DATA
      PATHS $ENV{GRK_DATA_ROOT} ${CMAKE_SOURCE_DIR}/../grok-test-data
      NO_CMAKE_FIND_ROOT_PATH
      )

    # Add repository where to find tests
    add_subdirectory(tests)

  else()
    message(FATAL_ERROR "You need build codec to run the tests")
  endif()
endif()

#-----------------------------------------------------------------------------
# install all targets referenced as GrokTargets
install(EXPORT GrokTargets DESTINATION ${GROK_INSTALL_PACKAGE_DIR})
configure_file( ${${GROK_NAMESPACE}_SOURCE_DIR}/cmake/GrokConfig.cmake.in
  ${${GROK_NAMESPACE}_BINARY_DIR}/GrokConfig.cmake
  @ONLY
)
install( FILES ${GROK_BINARY_DIR}/GrokConfig.cmake
  DESTINATION ${GROK_INSTALL_PACKAGE_DIR}
)

#-----------------------------------------------------------------------------
# install CHANGES and LICENSE
if(BUILD_DOC)
if(EXISTS ${GROK_SOURCE_DIR}/CHANGES)
  install(FILES CHANGES DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

include (cmake/GrokCPack.cmake)

#-----------------------------------------------------------------------------
# pkgconfig support
# enabled by default on Unix, disabled by default on other platforms
if(UNIX)
  option(BUILD_PKGCONFIG_FILES "Build and install pkg-config files" ON)
else()
  option(BUILD_PKGCONFIG_FILES "Build and install pkg-config files" OFF)
endif()
if(BUILD_PKGCONFIG_FILES)
   configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/lib/jp2/libgrokj2k.pc.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/libgrokj2k.pc @ONLY)
  install( FILES  ${CMAKE_CURRENT_BINARY_DIR}/libgrokj2k.pc DESTINATION
    ${CMAKE_INSTALL_LIBDIR}/pkgconfig )
endif()


# Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
    "MinSizeRel" "RelWithDebInfo")
endif()
