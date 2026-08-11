cmake_minimum_required(VERSION 3.24)
project(Capricorn-V129 LANGUAGES CXX)

# Qt deployment uses CMAKE_INSTALL_BINDIR to decide where Windows runtime
# binaries (Qt DLLs) are installed. Keep it aligned with the main EXE.
if(WIN32)
    set(CMAKE_INSTALL_BINDIR "." CACHE STRING "Install Windows runtime binaries next to the application EXE" FORCE)
endif()

add_subdirectory(frontend-qt)
