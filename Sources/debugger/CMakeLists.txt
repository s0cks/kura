set(GTK4_VERSION 4.22.4)
pkg_check_modules(GTK4  REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)
pkg_check_modules(EGL   REQUIRED IMPORTED_TARGET egl)

add_library(kura-debug INTERFACE)
target_link_libraries(kura-debug
  INTERFACE kura-runtime
            PkgConfig::GTK4
            PkgConfig::EPOXY
            PkgConfig::EGL
)

add_executable(kura-debugger
  main.cc)
target_link_libraries(kura-debugger
  PUBLIC kura-debug)

add_executable(kura-gtk4-gl-example
  gtk4-gl-example.c)
target_link_libraries(kura-gtk4-gl-example
  PUBLIC kura-debug)
