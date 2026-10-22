cmake_minimum_required(VERSION 3.21)
project(bbinstaller VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network)

# .bar manifests are deflated: use the system zlib, else the copy Qt itself is built with
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    set(BB_ZLIB ZLIB::ZLIB)
else()
    find_package(Qt6 REQUIRED COMPONENTS ZlibPrivate)
    set(BB_ZLIB Qt6::ZlibPrivate)
endif()

add_library(bbcore STATIC src/bb.cpp src/bb.h)
target_include_directories(bbcore PUBLIC src)
target_link_libraries(bbcore PUBLIC Qt6::Network PRIVATE ${BB_ZLIB})
if(NOT ZLIB_FOUND)
    target_compile_definitions(bbcore PRIVATE BB_QT_ZLIB)
endif()

qt_add_executable(bbinstaller WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
)
target_compile_definitions(bbinstaller PRIVATE BBINSTALLER_VERSION="${PROJECT_VERSION}")
target_link_libraries(bbinstaller PRIVATE bbcore Qt6::Widgets)
if(WIN32 OR APPLE)
    set_target_properties(bbinstaller PROPERTIES OUTPUT_NAME "BB Installer")
endif()
set_target_properties(bbinstaller PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.bbinstaller
    MACOSX_BUNDLE_BUNDLE_NAME "BB Installer"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

install(TARGETS bbinstaller BUNDLE DESTINATION . RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(tst_bb tests/tst_bb.cpp)
    target_link_libraries(tst_bb PRIVATE bbcore Qt6::Test)
    add_test(NAME tst_bb COMMAND tst_bb)
endif()
