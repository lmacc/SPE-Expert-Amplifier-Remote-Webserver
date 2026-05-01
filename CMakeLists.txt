cmake_minimum_required(VERSION 3.21)
project(spe-remote-qt LANGUAGES CXX VERSION 1.3.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# --- Qt ----------------------------------------------------------------------
find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core
    Network
    SerialPort
    WebSockets
    HttpServer
)
# Widgets + Gui are only required for the desktop GUI; daemon runs without them.
find_package(Qt6 6.4 COMPONENTS Gui Widgets)

qt_standard_project_setup()

# --- Shared headless core ----------------------------------------------------
add_library(spe_core STATIC
    src/AmpController.h
    src/AmpController.cpp
    src/WsBridge.h
    src/WsBridge.cpp
    src/HttpServer.h
    src/HttpServer.cpp
    src/ConfigStore.h
    src/ConfigStore.cpp
    src/Auth.h
    src/Auth.cpp
)
target_include_directories(spe_core PUBLIC src)
target_link_libraries(spe_core PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::SerialPort
    Qt6::WebSockets
    Qt6::HttpServer
)

# Embed the original web/ folder into the core library so both binaries ship
# the UI in-process — no separate web server or filesystem path required.
qt_add_resources(spe_core "web_resources"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    FILES
        resources/web/index.html
        resources/web/settings.html
        resources/web/css/app.css
        resources/web/css/settings.css
        resources/web/js/settings.js
        resources/web/justgage/justgage.js
        resources/web/justgage/raphael-2.1.4.min.js
)

# --- Daemon (headless, no Widgets) -------------------------------------------
# Safe on a Pi with no X server: links only Core + Network + SerialPort +
# WebSockets + HttpServer.
qt_add_executable(spe-remoted
    src/daemon_main.cpp
)
target_link_libraries(spe-remoted PRIVATE spe_core)

install(TARGETS spe-remoted RUNTIME DESTINATION bin)

# --- Desktop GUI -------------------------------------------------------------
if(Qt6Widgets_FOUND AND Qt6Gui_FOUND)
    qt_add_executable(spe-remote-qt
        src/main.cpp
        src/MainWindow.h
        src/MainWindow.cpp
        src/AmpFaceWidget.h
        src/AmpFaceWidget.cpp
        src/SettingsDialog.h
        src/SettingsDialog.cpp
        src/AboutDialog.h
        src/AboutDialog.cpp
        resources/resources.qrc
    )
    target_link_libraries(spe-remote-qt PRIVATE
        spe_core
        Qt6::Gui
        Qt6::Widgets
    )
    if(WIN32)
        set_target_properties(spe-remote-qt PROPERTIES WIN32_EXECUTABLE ON)
    endif()
    install(TARGETS spe-remote-qt
        BUNDLE  DESTINATION .
        RUNTIME DESTINATION bin
    )
else()
    message(STATUS "Qt6 Widgets/Gui not found — skipping desktop GUI build (daemon still built).")
endif()
