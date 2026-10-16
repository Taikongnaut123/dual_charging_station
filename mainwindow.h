#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "bridgeclient.h"
#include "mdcdata.h"
#include "stationcontroller.h"

class ControlPage;
class OverviewPage;
class PeripheralPage;
class PowerPage;

class QLabel;
class QListWidget;
class QResizeEvent;
class QTabWidget;
class QTimer;

/**
 * 充电桩显示屏主窗口。
 *
 * 结构：顶部状态栏 + 页签（5 页）+ 底部信息栏。
 *
 * 数据链路：
 *   mdc_node(ROS2) --/mdc_data--> mdc_bridge --UDP JSON--> BridgeClient
 *   --> StationController（状态派生 + 本地统计）--> 各页面刷新
 *
 * 本程序**不依赖 ROS2**，只依赖 mdc_bridge 进程；桥不在时可用环回模式演示。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /** 连接 mdc_bridge（绑定状态接收端口并指定桥的控制端口） */
    bool startBridge(quint16 cmdPort = mdcproto::kDefaultCmdPort,
                     quint16 statPort = mdcproto::kDefaultStatPort,
                     const QString &host = QStringLiteral("127.0.0.1"));

    /** 无真实桥时用内置仿真数据驱动界面（开发/演示用） */
    void setLoopbackEnabled(bool on);

    BridgeClient *bridgeClient() const { return m_bridge; }
    StationController *stationController() const { return m_controller; }

protected:
    /** 窗口变窄时隐藏底部提示，避免文字被裁切 */
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onLogMessage(int level, const QString &message);
    void onLinkStateChanged(BridgeClient::LinkState state);
    void onStatisticsChanged();
    void updateClock();

private:
    void buildUi();
    QWidget *buildHeader();
    QWidget *buildFooter();
    QWidget *buildLogPage();
    static QWidget *wrapInScrollArea(QWidget *page);
    void setupShortcuts();

    void setPill(QLabel *pill, const QString &text, const QString &level);
    void appendLog(int level, const QString &message);

    BridgeClient *m_bridge = nullptr;
    StationController *m_controller = nullptr;

    OverviewPage *m_overviewPage = nullptr;
    PowerPage *m_powerPage = nullptr;
    PeripheralPage *m_peripheralPage = nullptr;
    ControlPage *m_controlPage = nullptr;

    QTabWidget *m_tabs = nullptr;
    QListWidget *m_logList = nullptr;

    QLabel *m_clockTime = nullptr;
    QLabel *m_clockDate = nullptr;
    QLabel *m_pillLink = nullptr;
    QLabel *m_pillDevice = nullptr;
    QLabel *m_pillState = nullptr;

    QLabel *m_footBridge = nullptr;
    QLabel *m_footRate = nullptr;
    QLabel *m_footLost = nullptr;
    QLabel *m_footSessions = nullptr;
    QLabel *m_footEnergy = nullptr;
    QLabel *m_footLastLog = nullptr;

    QTimer *m_clockTimer = nullptr;
};

#endif // MAINWINDOW_H
