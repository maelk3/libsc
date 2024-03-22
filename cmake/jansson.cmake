include(ExternalProject)
include(GNUInstallDirs)

set(SC_HAVE_JSON 1 CACHE BOOL "using SC-built JANSSON")

set(jansson_url "https://github.com/akheron/jansson/releases/download/v2.14/jansson-2.14.tar.bz2")

set(JANSSON_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include)

if(BUILD_SHARED_LIBS)
  if(WIN32)
    set(JANSSON_LIBRARIES ${CMAKE_INSTALL_FULL_BINDIR}/${CMAKE_SHARED_LIBRARY_PREFIX}jansson${CMAKE_SHARED_LIBRARY_SUFFIX})
  else()
    set(JANSSON_LIBRARIES ${CMAKE_INSTALL_FULL_LIBDIR}/${CMAKE_SHARED_LIBRARY_PREFIX}jansson${CMAKE_SHARED_LIBRARY_SUFFIX})
  endif()
else()
    set(JANSSON_LIBRARIES ${CMAKE_INSTALL_FULL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}jansson${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()

set(jansson_cmake_args
-DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
-DJANSSON_BUILD_SHARED_LIBS:BOOL=${BUILD_SHARED_LIBS}
-DCMAKE_BUILD_TYPE=Release
-DJANSSON_EXAMPLES:BOOL=off
-DJANSSON_WITHOUT_TESTS:BOOL=on
-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
)

ExternalProject_Add(JANSSON
URL ${jansson_url}
CMAKE_ARGS ${jansson_cmake_args}
BUILD_BYPRODUCTS ${JANSSON_LIBRARIES}
CONFIGURE_HANDLED_BY_BUILD ON
USES_TERMINAL_DOWNLOAD true
USES_TERMINAL_UPDATE true
USES_TERMINAL_CONFIGURE true
USES_TERMINAL_BUILD true
USES_TERMINAL_INSTALL true
USES_TERMINAL_TEST true
)


# --- imported target

file(MAKE_DIRECTORY ${JANSSON_INCLUDE_DIRS})
# avoid race condition

add_library(jansson::jansson INTERFACE IMPORTED GLOBAL)
add_dependencies(jansson::jansson JANSSON)  # to avoid include directory race condition
target_link_libraries(jansson::jansson INTERFACE ${JANSSON_LIBRARIES})
target_include_directories(jansson::jansson INTERFACE ${JANSSON_INCLUDE_DIRS})
