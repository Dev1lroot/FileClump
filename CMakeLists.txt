# SPDX-License-Identifier: GPL-2.0-only
cmake_minimum_required(VERSION 3.16)
project(FileClump VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(Threads REQUIRED)

add_executable(fileclump
    src/main.cpp
    src/Scanner.cpp
    src/Models.cpp
    src/Treemap.cpp
    src/MainWindow.cpp
    assets/resources.qrc
)
target_compile_options(fileclump PRIVATE -Wall -Wextra)
target_link_libraries(fileclump PRIVATE Qt6::Widgets Threads::Threads)

include(GNUInstallDirs)
install(TARGETS fileclump RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES assets/fileclump.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
