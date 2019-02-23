TEMPLATE = lib
CONFIG += plugin release
QT += widgets designer
target.path = $$[QT_INSTALL_PLUGINS]/designer
DEFINES += DESIGNER_PLUGIN
INSTALLS += target
QMAKE_CXXFLAGS += -std=c++11
INCLUDEPATH += ../../
INCLUDEPATH += ../../../client/

# Input
#RESOURCES = resources.qrc

HEADERS += collection.h \
	../colorbutton.h colorbutton_plugin.h \
	../groupedtoolbutton.h groupedtoolbutton_plugin.h \
	../filmstrip.h filmstrip_plugin.h \
	../resizerwidget.h resizer_plugin.h \
	../brushpreview.h brushpreview_plugin.h \
	../tablettest.h tablettester_plugin.h \
	../spinner.h spinner_plugin.h

SOURCES += collection.cpp \
	../colorbutton.cpp colorbutton_plugin.cpp \
	../groupedtoolbutton.cpp groupedtoolbutton_plugin.cpp \
	../filmstrip.cpp filmstrip_plugin.cpp \
	../resizerwidget.cpp resizer_plugin.cpp \
	../brushpreview.cpp brushpreview_plugin.cpp \
	../tablettest.cpp tablettester_plugin.cpp \
	../spinner.cpp spinner_plugin.cpp

