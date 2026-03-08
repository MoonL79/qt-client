cmake_minimum_required(VERSION 3.19)
project(qt-client LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets WebSockets Test Network)
find_package(Qt6 REQUIRED COMPONENTS Core)

qt_standard_project_setup()

qt_add_executable(qt-client
    WIN32 MACOSX_BUNDLE
    main.cpp
    src/ui/main/widget.cpp
    src/ui/main/widget.h
    src/ui/main/widget.ui
    src/ui/login/loginwindow.cpp
    src/ui/login/loginwindow.h
    src/ui/login/loginwindow.ui
    src/ui/session/sessionwindow.h
    src/ui/session/sessionwindow.cpp
    src/ui/register/registerwindow.h
    src/ui/register/registerwindow.cpp
    src/ui/register/registerwindow.ui
    src/ui/test/logwindow.h
    src/ui/test/logwindow.cpp
    src/ui/settings/settingswindow.h
    src/ui/settings/settingswindow.cpp
    src/ui/friend/addfrienddialog.h
    src/ui/friend/addfrienddialog.cpp
    resources/resources.qrc
    src/network/websocketclient.h
    src/network/websocketclient.cpp
    src/network/protocol.h
    src/network/protocol.cpp
    src/network/profileapiclient.h
    src/network/profileapiclient.cpp
    src/auth/registerutils.h
    src/auth/registerutils.cpp
    src/session/session.h
    src/session/session.cpp
    src/session/usersession.h
    src/session/usersession.cpp
)

target_include_directories(qt-client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/login
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/main
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/session
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/register
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/test
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/settings
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/friend
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network
    ${CMAKE_CURRENT_SOURCE_DIR}/src/auth
    ${CMAKE_CURRENT_SOURCE_DIR}/src/session
)

target_link_libraries(qt-client
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt6::Network
        Qt6::WebSockets
)
target_link_libraries(qt-client PRIVATE Qt6::Core)

enable_testing()

qt_add_executable(registerutils_test
    test/registerutils_test.cpp
    src/auth/registerutils.cpp
    src/auth/registerutils.h
    src/network/protocol.cpp
    src/network/protocol.h
)

target_include_directories(registerutils_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/auth
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network
)

target_link_libraries(registerutils_test
    PRIVATE
        Qt::Core
        Qt::Test
)

add_test(NAME registerutils_test COMMAND registerutils_test)

include(GNUInstallDirs)

install(TARGETS qt-client
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET qt-client
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
