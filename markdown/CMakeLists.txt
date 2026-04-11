cmake_minimum_required(VERSION 3.16) # Qt requires CMake 3.16
project(qmarkdowntextedit LANGUAGES CXX VERSION 1.0.0)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# add option to disable test executable
option(QMARKDOWNTEXTEDIT_EXE "Build test executable" ON)

# find qt
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} OPTIONAL_COMPONENTS Quick)

# needed for windows
if(WIN32)
    set(INTL_LDFLAGS -lintl)
endif(WIN32)

# QMarkdownTextEdit library
set(RC_FILES
    media.qrc
)

# Translations arent loaded so don't include them
set(TS_FILES
    trans/qmarkdowntextedit_de.ts
    trans/qmarkdowntextedit_ur.ts
    trans/qmarkdowntextedit_zh_CN.ts
)

set(QMARKDOWNTEXTEDIT_SOURCES
    ${RC_FILES}
    linenumberarea.h # We need to keep this here, otherwise the build fails
    markdownhighlighter.cpp
    qmarkdowntextedit.cpp
    qownlanguagedata.cpp
    qownlanguagedata.h
    qplaintexteditsearchwidget.cpp
    qplaintexteditsearchwidget.ui
)
set(QMARKDOWNTEXTEDIT_HEADERS
    markdownhighlighter.h
    qmarkdowntextedit.h
    qplaintexteditsearchwidget.h
)

add_library(qmarkdowntextedit ${QMARKDOWNTEXTEDIT_SOURCES})
set_target_properties(qmarkdowntextedit PROPERTIES
    PUBLIC_HEADER "${QMARKDOWNTEXTEDIT_HEADERS}"
)

target_link_libraries(qmarkdowntextedit PUBLIC
    Qt${QT_VERSION_MAJOR}::Widgets
    ${INTL_LDFLAGS}
)

if (Qt${QT_VERSION_MAJOR}Quick_FOUND)
    target_link_libraries(qmarkdowntextedit PUBLIC Qt${QT_VERSION_MAJOR}::Quick)

    add_executable(QtQuickExample examples/qml/example.cpp examples/qml/ressources.qrc)
    target_link_libraries(QtQuickExample PRIVATE Qt${QT_VERSION_MAJOR}::Quick qmarkdowntextedit)
endif()

# QMarkdownTextEdit executable
if(QMARKDOWNTEXTEDIT_EXE)
    set(SOURCE_FILES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
    )

    add_executable(qmarkdowntextedit-exe ${SOURCE_FILES})
    set_target_properties(qmarkdowntextedit-exe PROPERTIES OUTPUT_NAME "qmarkdowntextedit")
    target_link_libraries(qmarkdowntextedit-exe PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        ${INTL_LDFLAGS}
        qmarkdowntextedit
    )
endif()

include(GNUInstallDirs) # Doesn't fail on windows

# Install the lib
install(TARGETS qmarkdowntextedit
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Add PkgConfig config file
configure_file(qmarkdowntextedit.pc.in ${CMAKE_BINARY_DIR}/qmarkdowntextedit.pc @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/qmarkdowntextedit.pc DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig)

# Install exe
if(QMARKDOWNTEXTEDIT_EXE)
    install(TARGETS qmarkdowntextedit-exe DESTINATION bin)
endif()
