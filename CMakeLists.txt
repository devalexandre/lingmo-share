cmake_minimum_required(VERSION 3.16)
project(lingmo-share VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

option(BUILD_TESTING "Build the tests" ON)

# QApplication: tray icon (StatusNotifierItem) and its menu
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network DBus Qml Quick LinguistTools)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-xfixes)
pkg_check_modules(QRENCODE REQUIRED IMPORTED_TARGET libqrencode)

set(TRANSLATIONS_DIR ${CMAKE_INSTALL_FULL_DATADIR}/lingmo-share/translations)

# Protocols and settings, shared by the daemon, the QML plugin and the tests
add_library(share-core STATIC
    src/common/shareconfig.cpp
    src/common/safename.cpp
    src/common/qrcode.cpp
    src/localsend/identity.cpp
    src/localsend/httpserver.cpp
    src/localsend/localsend.cpp
    src/localsend/sender.cpp
    src/kdeconnect/kdeconnect.cpp
)
set_target_properties(share-core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(share-core PUBLIC src)
target_link_libraries(share-core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Network Qt6::DBus Qt6::Qml
    OpenSSL::Crypto PkgConfig::QRENCODE)
target_compile_definitions(share-core PUBLIC TRANSLATIONS_DIR="${TRANSLATIONS_DIR}")

add_executable(lingmo-share
    src/daemon/main.cpp
    src/daemon/daemon.cpp
    src/daemon/devicesmodel.cpp
    src/daemon/transfers.cpp
    src/daemon/jobviewserver.cpp
    src/daemon/dragwatcher.cpp
    src/daemon/feedback.cpp
    src/daemon/smsmodel.cpp
    resources.qrc
)
target_link_libraries(lingmo-share PRIVATE
    share-core
    Qt6::Widgets Qt6::Quick
    PkgConfig::XCB
)
target_compile_definitions(lingmo-share PRIVATE LINGMO_SHARE_VERSION="${PROJECT_VERSION}")

foreach(f SettingsPage PhoneGuide QrCard)
    set_source_files_properties(qml/module/${f}.qml PROPERTIES QT_RESOURCE_ALIAS ${f}.qml)
endforeach()

# Lingmo.Share: what the "Phone and devices" page of Lingmo Settings uses
qt_add_qml_module(lingmo-share-qml
    URI Lingmo.Share
    VERSION 1.0
    PLUGIN_TARGET lingmo-share-qml
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/Lingmo/Share
    SOURCES
        src/qmlplugin/sharesettings.cpp
        src/qmlplugin/sharesettings.h
        src/qmlplugin/qrimage.cpp
        src/qmlplugin/qrimage.h
    QML_FILES
        qml/module/SettingsPage.qml
        qml/module/PhoneGuide.qml
        qml/module/QrCard.qml
    RESOURCES
        images/phone.svg
        images/send.svg
)
target_include_directories(lingmo-share-qml PRIVATE src/qmlplugin)
target_link_libraries(lingmo-share-qml PRIVATE share-core Qt6::Quick)

# lrelease only: building never rewrites the .ts sources
qt_add_translation(QM_FILES translations/lingmo-share_pt_BR.ts)
add_custom_target(translations ALL DEPENDS ${QM_FILES})
install(FILES ${QM_FILES} DESTINATION ${TRANSLATIONS_DIR})

install(TARGETS lingmo-share RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS lingmo-share-qml LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/qt6/qml/Lingmo/Share)
install(FILES ${CMAKE_BINARY_DIR}/qml/Lingmo/Share/qmldir
              ${CMAKE_BINARY_DIR}/qml/Lingmo/Share/lingmo-share-qml.qmltypes
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/qt6/qml/Lingmo/Share)
install(FILES data/lingmo-share.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/lingmo-share-daemon.desktop DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/autostart)
install(FILES data/com.lingmo.Share.service DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(FILES images/lingmo-share.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(tst_safename tests/tst_safename.cpp)
    target_link_libraries(tst_safename PRIVATE share-core Qt6::Test)
    add_test(NAME tst_safename COMMAND tst_safename)

    # Two LocalSend peers on loopback (no multicast needed): send, PIN, decline, traversal
    add_executable(tst_localsend tests/tst_localsend.cpp)
    target_link_libraries(tst_localsend PRIVATE share-core Qt6::Test)
    add_test(NAME tst_localsend COMMAND tst_localsend)
    set_tests_properties(tst_localsend PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C")

    # A fake kdeconnectd for the D-Bus client and UI tests
    add_executable(fake-kdeconnectd tests/fake-kdeconnectd.cpp)
    target_link_libraries(fake-kdeconnectd PRIVATE Qt6::Core Qt6::DBus)

    # com.lingmo.Settings /Theme (light or dark) for the screenshots
    add_executable(fake-theme tests/fake-theme.cpp)
    target_link_libraries(fake-theme PRIVATE Qt6::Gui Qt6::DBus)

    # Drags a file from a window: drives the drop-bubble test under Xvfb
    add_executable(drag-source tests/drag-source.cpp)
    target_link_libraries(drag-source PRIVATE Qt6::Widgets)
endif()
