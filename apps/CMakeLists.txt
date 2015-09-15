
# Copyright (c) 2013-2015, EPFL/Blue Brain Project
#                     Raphael Dumusc <raphael.dumusc@epfl.ch>
#                     Daniel Nachbaur <daniel.nachbaur@epfl.ch>

add_subdirectory(DesktopStreamer)

if(Qt5Qml_FOUND AND Qt5Quick_FOUND AND Qt5Quick_VERSION VERSION_GREATER 5.3)
  add_subdirectory(QmlStreamer)
endif()

if(GLUT_FOUND)
  add_subdirectory(SimpleStreamer)
endif()
