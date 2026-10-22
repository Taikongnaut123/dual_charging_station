#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "bridgeclient.h"
#include "mdcdata.h"
#include "stationcontroller.h"

class OverviewPage;
class StationPage;

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

    /** 绑定状态接收端口（单向接收，界面不发送任何东西） */
    bool startBridge(quint16 statPort = mdcproto::kDefaultStatPort);

    /** 无真实桥时用内置仿真数据驱动界面（开发/演示用） */
    void setLoopbackEnabled(bool on);

    /**
     * 全屏显示。
     *
     * 不能直接用 showFullScreen()：Qt 在 X11 下是靠 _NET_WM_STATE_FULLSCREEN
     * 让窗口管理器帮忙改尺寸的，一体屏上往往直接 xinit 起程序、根本没有 WM，
     * 于是窗口停在构造时的 1360x800（或缩放后的尺寸），四周留黑边。
     * 这里等一会儿比对实际几何，没铺满就自己贴到屏幕尺寸上。
     */
    void enterFullScreen();

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
    StationPage *m_stationPage = nullptr;

    QTabWidget *m_tabs = nullptr;
    QListWidget *m_logList = nullptr;

    QLabel *m_clockTime = nullptr;
    QLabel *m_clockDate = nullptr;
    QLabel *m_pillLink = nullptr;
    QLabel *m_pillDevice = nullptr;
    QLabel *m_pillState = nullptr;

    QLabel *m_headerSub = nullptr; // 标题下的英文副标题，矮屏时隐藏

    QLabel *m_footBridge = nullptr;
    QLabel *m_footRate = nullptr;
    QLabel *m_footLost = nullptr;
    QLabel *m_footSessions = nullptr;
    QLabel *m_footEnergy = nullptr;
    QLabel *m_footLastLog = nullptr;
    QTimer *m_clockTimer = nullptr;
};

#endif // MAINWINDOW_H
