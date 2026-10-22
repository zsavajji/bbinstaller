cmake_minimum_required(VERSION 3.21)
project(bbinstaller VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(BBINSTALLER_FUSION "Always use Qt's Fusion style (for the self-contained AppImage)" OFF)

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
if(BBINSTALLER_FUSION)
    target_compile_definitions(bbinstaller PRIVATE BBINSTALLER_FUSION)
endif()
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
if(UNIX AND NOT APPLE)
    install(FILES packaging/bbinstaller.desktop DESTINATION share/applications)
    install(FILES packaging/bbinstaller.png DESTINATION share/icons/hicolor/256x256/apps)
endif()

# .deb/.rpm against the distro's own Qt, so KDE's Breeze and other system themes apply
set(CPACK_PACKAGE_NAME bbinstaller)
set(CPACK_PACKAGE_VENDOR bbinstaller)
set(CPACK_PACKAGE_CONTACT "zsavajji <zsavajji@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Install .bar apps on BlackBerry 10 over USB, no developer mode needed")
set(CPACK_PACKAGE_HOMEPAGE_URL https://github.com/zsavajji/bbinstaller)
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_STRIP_FILES ON)
set(CPACK_DEBIAN_PACKAGE_SECTION utils)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Qt loads OpenSSL and its display plugins at runtime, so nothing links them: name them (t64 on newer releases)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libssl3t64 | libssl3, qt6-qpa-plugins")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_RPM_PACKAGE_LICENSE GPL-3.0-only)
set(CPACK_RPM_PACKAGE_GROUP Applications/System)
set(CPACK_RPM_PACKAGE_REQUIRES openssl-libs)
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/applications /usr/share/icons /usr/share/icons/hicolor /usr/share/icons/hicolor/256x256
    /usr/share/icons/hicolor/256x256/apps)
include(CPack)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(tst_bb tests/tst_bb.cpp)
    target_link_libraries(tst_bb PRIVATE bbcore Qt6::Test)
    add_test(NAME tst_bb COMMAND tst_bb)
endif()
