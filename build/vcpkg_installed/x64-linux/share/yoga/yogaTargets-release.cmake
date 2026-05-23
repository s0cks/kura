#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "yoga::yogacore" for configuration "Release"
set_property(TARGET yoga::yogacore APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(yoga::yogacore PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libyogacore.a"
  )

list(APPEND _cmake_import_check_targets yoga::yogacore )
list(APPEND _cmake_import_check_files_for_yoga::yogacore "${_IMPORT_PREFIX}/lib/libyogacore.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
