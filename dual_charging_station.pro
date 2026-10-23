QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

TEMPLATE = app
# 可执行文件名带 chric_software_ 前缀。板子上同时跑着 ROS 节点、桥、界面，
# ps / top / pgrep 里看到的名字要能一眼区分出哪些是本项目的进程。
TARGET   = chric_software_dual_charging_station

# shared/mdc_protocol.h 是界面与 mdc_bridge 之间 UDP JSON 协议的唯一契约，
# 两个构建系统（qmake 与 colcon）都引用同一份头文件，避免手抄键名对不上。
INCLUDEPATH += $$PWD
INCLUDEPATH += $$PWD/shared
DEPENDPATH  += $$PWD

# 界面采用纯代码 + QPainter 自绘实现，不使用 .ui 窗体文件。
# 数据来自 mdc_bridge 进程（UDP JSON），本程序不依赖 ROS2。
SOURCES += \
    main.cpp \
    mainwindow.cpp \
    bridgeclient.cpp \
    stationcontroller.cpp \
    theme.cpp \
    pages/overviewpage.cpp \
    pages/stationpage.cpp \
    widgets/card.cpp \
    widgets/chargecurvechart.cpp \
    widgets/circulargauge.cpp \
    widgets/levelbar.cpp \
    widgets/lightindicator.cpp \
    widgets/metrictile.cpp \
    widgets/stagetimeline.cpp

HEADERS += \
    mainwindow.h \
    bridgeclient.h \
    mdcdata.h \
    stationcontroller.h \
    theme.h \
    shared/mdc_protocol.h \
    pages/overviewpage.h \
    pages/pageutils.h \
    pages/stationpage.h \
    widgets/card.h \
    widgets/chargecurvechart.h \
    widgets/circulargauge.h \
    widgets/levelbar.h \
    widgets/lightindicator.h \
    widgets/metrictile.h \
    widgets/stagetimeline.h

TRANSLATIONS += \
    dual_charging_station_zh_CN.ts
CONFIG += lrelease
CONFIG += embed_translations

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
