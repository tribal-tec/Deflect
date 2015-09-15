
# Copyright (c) 2013-2015, EPFL/Blue Brain Project
#                     Daniel Nachbaur <daniel.nachbaur@epfl.ch>

set(QMLSTREAMER_MOC_PUBLIC_HEADERS QmlStreamer.h)
set(QMLSTREAMER_MOC_HEADERS EventHandler.h)
set(QMLSTREAMER_SOURCES EventHandler.cpp QmlStreamer.cpp)
set(QMLSTREAMER_LINK_LIBRARIES
  PUBLIC Deflect Qt5::Gui
  PRIVATE Qt5::Qml Qt5::Quick)
common_library(QmlStreamer)

set(EXAMPLEQMLSTREAMER_SOURCES main.cpp)
set(EXAMPLEQMLSTREAMER_RESOURCES resources.qrc)
set(EXAMPLEQMLSTREAMER_LINK_LIBRARIES QmlStreamer)
common_application(ExampleQmlStreamer)
