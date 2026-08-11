cmake_minimum_required(VERSION 3.24)
project(Capricorn-V129 VERSION 129.0.0 LANGUAGES CXX)

# IMPORTANT: Qt deployment derives QT_DEPLOY_BIN_DIR from CMAKE_INSTALL_BINDIR
# when Qt Core is found. Set it before find_package() so the EXE and all Qt
# runtime DLLs are deployed to the same directory on Windows.
if(WIN32)
    set(CMAKE_INSTALL_BINDIR "." CACHE STRING "Install Windows runtime binaries next to the application EXE" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network Svg Sql)
qt_standard_project_setup()

# Large avatar SVGs are deliberately NOT embedded in resources.qrc.
# Embedding the ~180 MB avatar set makes rcc generate a huge qrc_resources.cpp
# translation unit that can exhaust MinGW cc1plus memory. Keep those files as
# runtime assets and copy/install them next to the executable instead.
set(CAPRICORN_AVATAR_ASSET_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/avatars")


set(CAPRICORN_FRONTEND_SOURCES
    src/main.cpp
    src/AppData.cpp src/AppData.h
    src/ChatStore.cpp src/ChatStore.h
    src/CoreClient.cpp src/CoreClient.h
    src/ProcessSupervisor.cpp src/ProcessSupervisor.h
    src/AvatarWidget.cpp src/AvatarWidget.h
    src/CircularProgressWidget.cpp src/CircularProgressWidget.h
    src/PetWindow.cpp src/PetWindow.h
    src/ChatWindow.cpp src/ChatWindow.h
    src/VoiceRecognitionClient.cpp src/VoiceRecognitionClient.h
    src/MainWindow.cpp src/MainWindow.h
    resources.qrc
)

if(WIN32)
    configure_file(resources/icon.rc.in "${CMAKE_CURRENT_BINARY_DIR}/icon.rc" @ONLY)
    list(APPEND CAPRICORN_FRONTEND_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/icon.rc")
endif()

qt_add_executable(Capricorn-V129 WIN32 ${CAPRICORN_FRONTEND_SOURCES})

target_link_libraries(Capricorn-V129 PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Svg Qt6::Sql
)

if(WIN32)
    target_link_libraries(Capricorn-V129 PRIVATE ole32 uuid winhttp winmm)
endif()

# Optional QA probes whose source files are not part of the release source package were removed in V129.

target_compile_definitions(Capricorn-V129 PRIVATE CAPRICORN_VERSION="129.0.0")

if(MSVC)
    target_compile_options(Capricorn-V129 PRIVATE /W4 /permissive- /EHsc /Zc:__cplusplus /utf-8)
else()
    target_compile_options(Capricorn-V129 PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    set_target_properties(Capricorn-V129 PROPERTIES OUTPUT_NAME "Capricorn-V129")
endif()

install(TARGETS Capricorn-V129 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY "${CAPRICORN_AVATAR_ASSET_SOURCE_DIR}/"
    DESTINATION "assets/avatars"
    FILES_MATCHING PATTERN "*.svg"
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../core-go/CapricornCore-V129.exe" DESTINATION core)

qt_generate_deploy_app_script(
    TARGET Capricorn-V129
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

# Keep plugin discovery deterministic when the executable lives at the install
# root. The build script validates this file before creating the ZIP.
if(WIN32)
    install(CODE [=[
        file(WRITE "${CMAKE_INSTALL_PREFIX}/qt.conf" "[Paths]\nPrefix=.\nPlugins=plugins\n")
    ]=])
endif()
