# Required cmake version
cmake_minimum_required(VERSION 3.5.0)
cmake_policy(SET CMP0048 NEW)

if (NOT DEFINED CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif()
# Specify search path for CMake modules to be loaded by include()
# and find_package()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")

### Add defaults for cmake
# These defaults need to be included before the project() call.
include(DefineCMakeDefaults)

# This will provide -DCMAKE_BUILD_TYPE=Profiling
# and -DCMAKE_BUILD_TYPE=AddressSanitizer
include(DefineCompilerFlags)

project(rtprocess VERSION 0.11.0 LANGUAGES CXX)

# SOVERSION scheme: MAJOR.MINOR.PATCH
#   If there was an incompatible interface change:
#     Increment MAJOR. Set MINOR and PATCH to 0
#   If there was a compatible interface change:
#     Increment MINOR. Set PATCH to 0
#   If the source code was changed, but there were no interface changes:
#     Increment PATCH.
set(LIBRARY_VERSION_MAJOR 0)
set(LIBRARY_VERSION_MINOR 0)
set(LIBRARY_VERSION_PATCH 1)
set(LIBRARY_VERSION "${LIBRARY_VERSION_MAJOR}.${LIBRARY_VERSION_MINOR}.${LIBRARY_VERSION_PATCH}")
set(LIBRARY_SOVERSION ${LIBRARY_VERSION_MAJOR})

# include cmake files
include(DefineOptions.cmake)
include(CompilerChecks.cmake)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CPackConfig.cmake)

# By default we don't use a specific processor target, so PROC_TARGET_NUMBER is set to 0.
# Specify other values to optimize for specific processor architecture as listed in ProcessorTargets.cmake:
set(PROC_TARGET_NUMBER 0 CACHE STRING "Selected target processor from the list taken from ProcessorTargets.cmake")
# Loads the ProcessorTargets list:
include(ProcessorTargets.cmake)
set(PROC_FLAGS "" CACHE STRING "Target processor related build/link flags")
if(NOT(PROC_TARGET_NUMBER EQUAL 0))
    set(PROC_FLAGS ${PROC_TARGET_${PROC_TARGET_NUMBER}_FLAGS})
endif()

add_subdirectory(src)
add_subdirectory(pkgconfig)

# cmake config files
if (WIN32)
    set(RTPROCESS_LIBRARY_NAME ${CMAKE_SHARED_LIBRARY_PREFIX}${PROJECT_NAME}${CMAKE_IMPORT_LIBRARY_SUFFIX})
else()
    set(RTPROCESS_LIBRARY_NAME ${CMAKE_SHARED_LIBRARY_PREFIX}${PROJECT_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX})
endif()

write_basic_package_version_file(${PROJECT_NAME}-config-version.cmake
                                 COMPATIBILITY
                                     AnyNewerVersion)

install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
        DESTINATION
            ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
        COMPONENT
            devel)

# Add 'make dist' target which makes sure to invoke cmake before
add_custom_target(dist
                  COMMAND ${CMAKE_MAKE_PROGRAM} package_source)
