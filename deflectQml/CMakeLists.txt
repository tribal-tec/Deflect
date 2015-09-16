
# Copyright (c) 2015, EPFL/Blue Brain Project
#                     Daniel Nachbaur <daniel.nachbaur@epfl.ch>

set(DEFLECTQML_MOC_PUBLIC_HEADERS QmlStreamer.h)
set(DEFLECTQML_MOC_HEADERS EventHandler.h)
set(DEFLECTQML_SOURCES EventHandler.cpp QmlStreamer.cpp)
set(DEFLECTQML_LINK_LIBRARIES
  PUBLIC Deflect Qt5::Quick PRIVATE Qt5::Qml)

common_library(DeflectQml)
