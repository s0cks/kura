#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "yoga::yogacore" for configuration "Debug"
set_property(TARGET yoga::yogacore APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(yoga::yogacore PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/lib/libyogacore.a"
  )

list(APPEND _cmake_import_check_targets yoga::yogacore )
list(APPEND _cmake_import_check_files_for_yoga::yogacore "${_IMPORT_PREFIX}/debug/lib/libyogacore.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
