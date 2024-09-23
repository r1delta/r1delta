#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "mimalloc" for configuration "RelWithDebInfo"
set_property(TARGET mimalloc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(mimalloc PROPERTIES
  IMPORTED_IMPLIB_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/mimalloc.lib"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/bin/mimalloc.dll"
  )

list(APPEND _IMPORT_CHECK_TARGETS mimalloc )
list(APPEND _IMPORT_CHECK_FILES_FOR_mimalloc "${_IMPORT_PREFIX}/lib/mimalloc.lib" "${_IMPORT_PREFIX}/bin/mimalloc.dll" )

# Import target "mimalloc-static" for configuration "RelWithDebInfo"
set_property(TARGET mimalloc-static APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(mimalloc-static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/mimalloc-2.1/mimalloc-static.lib"
  )

list(APPEND _IMPORT_CHECK_TARGETS mimalloc-static )
list(APPEND _IMPORT_CHECK_FILES_FOR_mimalloc-static "${_IMPORT_PREFIX}/lib/mimalloc-2.1/mimalloc-static.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
