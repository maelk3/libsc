option( SC_ENABLE_MPI "use MPI library" OFF )
option( SC_ENABLE_OPENMP "use OpenMP" OFF )

option( SC_USE_INTERNAL_ZLIB "build ZLIB" OFF )
option( SC_USE_INTERNAL_JSON "build Jansson" OFF )

option( SC_BUILD_SHARED_LIBS "build shared libsc" OFF )
option( SC_BUILD_TESTING "build libsc self-tests" ON )
option( SC_TEST_WITH_VALGRIND "run self-tests with valgrind" OFF )

set( SC_MPI_VERSION "legacy" CACHE STRING "Select the MPI standard that SC should export" )
set_property( CACHE SC_MPI_VERSION PROPERTY STRINGS "legacy" ">2.2" )

get_property( SC_MPI_VERSION_OPTIONS CACHE SC_MPI_VERSION PROPERTY STRINGS )
if( NOT SC_MPI_VERSION IN_LIST SC_MPI_VERSION_OPTIONS )
  string( REPLACE ";"  "\", \"" SC_MPI_VERSION_OPTIONS_STR "${SC_MPI_VERSION_OPTIONS}" )
  message( FATAL_ERROR "Wrong value for option \"SC_MPI_VERSION\"\
, supported values are \"${SC_MPI_VERSION_OPTIONS_STR}\" but ${SC_MPI_VERSION} was given." )
endif()

set_property(DIRECTORY PROPERTY EP_UPDATE_DISCONNECTED true)

# Necessary for shared library with Visual Studio / Windows oneAPI
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS true)

# --- auto-ignore build directory
if(NOT PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  file(GENERATE OUTPUT .gitignore CONTENT "*")
endif()

# We are enabling this shortcut even though we recommend to
# use -DSC_ENABLE_MPI:BOOL=... on the cmake command line.
if (DEFINED mpi)
  set_property(CACHE SC_ENABLE_MPI PROPERTY VALUE "${mpi}")
endif()
