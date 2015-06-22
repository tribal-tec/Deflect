
# Copyright (c) 2013-2015, EPFL/Blue Brain Project
#                     Daniel Nachbaur <daniel.nachbaur@epfl.ch>
#                     Raphael Dumusc <raphael.dumusc@epfl.ch>
#
# Generates a mock library used by the cpp unit tests.

set(MOCK_HEADERS
  MinimalGlobalQtApp.h
)

set(MOCK_MOC_HEADERS
  MockNetworkListener.h
)

qt5_wrap_cpp(MOC_OUTFILES ${MOCK_MOC_HEADERS})

set(MOCK_SOURCES
  ${MOC_OUTFILES}
  MockNetworkListener.cpp
)

set(IGNORE_LIB_TARGETS Mock)

set(MOCK_LINK_LIBRARIES ${Qt5Core_LIBRARIES} ${Qt5Network_LIBRARIES})
common_library(Mock)
set_target_properties(Mock PROPERTIES
  COMPILE_DEFINITIONS "DEFLECT_SHARED")
