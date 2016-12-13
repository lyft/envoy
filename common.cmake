set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb3 -fno-omit-frame-pointer -Wall -Wextra -Werror -Wnon-virtual-dtor -Woverloaded-virtual -Wold-style-cast -std=c++0x")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "4.9")
    message(FATAL_ERROR "gcc >= 4.9 required for regex support")
  endif()
endif()

option(ENVOY_CODE_COVERAGE "build with code coverage intrumentation" OFF)
if (ENVOY_CODE_COVERAGE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
  add_definitions(-DCOVERAGE)
endif()

option(ENVOY_DEBUG "build debug binaries" ON)
if (ENVOY_DEBUG)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0")
  add_definitions(-DDEBUG)
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
  add_definitions(-DNDEBUG)
endif()

option(ENVOY_SANITIZE "build with address sanitizer" OFF)
if (ENVOY_SANITIZE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(ENVOY_TCMALLOC OFF CACHE BOOL "" FORCE)
endif()

option(ENVOY_TCMALLOC "build with tcmalloc" ON)
if (ENVOY_TCMALLOC)
  add_definitions(-DTCMALLOC)
endif()

option(ENVOY_STRIP "strip symbols from binaries" OFF)

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${ENVOY_COTIRE_MODULE_DIR}")
include(cotire)
