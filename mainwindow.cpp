#include "mainwindow.h"

#include "pages/overviewpage.h"
#include "pages/stationpage.h"
#include "theme.h"
#include "widgets/card.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShortcut>
#include <QTabWidget>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>

namespace
{
    const int kMaxLogItems = 300;

    /** 底部信息栏的一行「键 值」 */
    QWidget *footerItem(const QString &key, QLabel *value)
    {
        auto *item = new QWidget;
        auto *lay = new QHBoxLayout(item);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(5);

        auto *k = new QLabel(key);
        k->setObjectName(QStringLiteral("rowKey"));
        lay->addWidget(k, 0, Qt::AlignVCenter);
        lay->addWidget(value, 0, Qt::AlignVCenter);
        return item;
    }

    QLabel *footerValue(const QString &initial = QStringLiteral("--"))
    {
        auto *l = new QLabel(initial);
        l->setObjectName(QStringLiteral("rowVal"));
        return l;
    }
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("双轮足机器人充电桩 · 显示屏"));
    setMinimumSize(1024, 620);
    resize(1360, 800);

    m_bridge = new BridgeClient(this);
    m_controller = new StationController(m_bridge, this);

    connect(m_bridge, &BridgeClient::logMessage, this, &MainWindow::onLogMessage);
    connect(m_bridge, &BridgeClient::linkStateChanged, this, &MainWindow::onLinkStateChanged);
    connect(m_controller, &StationController::statisticsChanged,
            this, &MainWindow::onStatisticsChanged);

    buildUi();
    setupShortcuts();

    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(1000);
    connect(m_clockTimer, &QTimer::timeout, this, &MainWindow::updateClock);
    m_clockTimer->start();
    updateClock();

    onLinkStateChanged(m_bridge->linkState());
    onStatisticsChanged();

    appendLog(0, QStringLiteral("显示屏程序启动"));
    appendLog(0, QStringLiteral("数据链路：mdc_node(ROS2) → /mdc_data → mdc_bridge → UDP → 本程序"));
}

MainWindow::~MainWindow() = default;

bool MainWindow::startBridge(quint16 statPort)
{
    return m_bridge->start(statPort);
}

void MainWindow::setLoopbackEnabled(bool on)
{
    m_bridge->setLoopbackEnabled(on);
}

// ------------------------------------------------------------------
// 界面构建
// ------------------------------------------------------------------

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("root"));
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(16, 14, 16, 12);
    root->setSpacing(10);

    root->addWidget(buildHeader());

    m_overviewPage = new OverviewPage(m_controller);
    m_stationPage = new StationPage(m_controller);

    m_tabs = new QTabWidget;
    m_tabs->setObjectName(QStringLiteral("mainTabs"));
    m_tabs->addTab(wrapInScrollArea(m_overviewPage), QStringLiteral("充电总览"));
    m_tabs->addTab(wrapInScrollArea(m_stationPage), QStringLiteral("充电桩状态"));
    m_tabs->addTab(buildLogPage(), QStringLiteral("运行日志"));
    root->addWidget(m_tabs, 1);

    root->addWidget(buildFooter());
}

QWidget *MainWindow::wrapInScrollArea(QWidget *page)
{
    auto *container = new QWidget;
    auto *lay = new QVBoxLayout(container);
    lay->setContentsMargins(14, 14, 14, 14);
    lay->addWidget(page);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(container);
    return scroll;
}

QWidget *MainWindow::buildHeader()
{
    auto *header = new QWidget;
    auto *lay = new QHBoxLayout(header);
    lay->setContentsMargins(4, 0, 4, 0);
    lay->setSpacing(10);

    auto *titleBox = new QVBoxLayout;
    titleBox->setSpacing(1);
    auto *title = new QLabel(QStringLiteral("双轮足机器人充电桩"));
    title->setObjectName(QStringLiteral("appTitle"));
    auto *sub = new QLabel(QStringLiteral("DUAL-WHEEL-LEGGED ROBOT CHARGING STATION · MDC"));
    sub->setObjectName(QStringLiteral("appSub"));
    titleBox->addWidget(title);
    titleBox->addWidget(sub);
    lay->addLayout(titleBox);
    lay->addStretch(1);

    const auto makePill = []
    {
        auto *pill = new QLabel;
        pill->setObjectName(QStringLiteral("pill"));
        return pill;
    };
    m_pillLink = makePill();
    m_pillDevice = makePill();
    m_pillState = makePill();
    lay->addWidget(m_pillLink, 0, Qt::AlignVCenter);
    lay->addWidget(m_pillDevice, 0, Qt::AlignVCenter);
    lay->addWidget(m_pillState, 0, Qt::AlignVCenter);

    auto *clockBox = new QVBoxLayout;
    clockBox->setSpacing(1);
    m_clockTime = new QLabel(QStringLiteral("--:--:--"));
    m_clockTime->setObjectName(QStringLiteral("clockTime"));
    m_clockTime->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_clockDate = new QLabel;
    m_clockDate->setObjectName(QStringLiteral("clockDate"));
    m_clockDate->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    clockBox->addWidget(m_clockTime);
    clockBox->addWidget(m_clockDate);

    lay->addSpacing(8);
    lay->addLayout(clockBox);
    return header;
}

QWidget *MainWindow::buildLogPage()
{
    auto *card = new Card(QStringLiteral("运行日志"));
    card->setBadge(QStringLiteral("实时"), QStringLiteral("info"));

    m_logList = new QListWidget;
    m_logList->setObjectName(QStringLiteral("logList"));
    m_logList->setSelectionMode(QAbstractItemView::NoSelection);
    m_logList->setFocusPolicy(Qt::NoFocus);
    m_logList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    card->addWidget(m_logList, 1);

    auto *container = new QWidget;
    auto *lay = new QVBoxLayout(container);
    lay->setContentsMargins(14, 14, 14, 14);
    lay->addWidget(card);
    return container;
}

QWidget *MainWindow::buildFooter()
{
    auto *footer = new QWidget;
    auto *lay = new QHBoxLayout(footer);
    lay->setContentsMargins(4, 0, 4, 0);
    lay->setSpacing(12);

    m_footBridge = footerValue(QStringLiteral("未连接"));
    m_footRate = footerValue(QStringLiteral("0 Hz"));
    m_footLost = footerValue(QStringLiteral("0"));
    m_footSessions = footerValue(QStringLiteral("0 次"));
    m_footEnergy = footerValue(QStringLiteral("0.0 kWh"));

    lay->addWidget(footerItem(QStringLiteral("数据链路"), m_footBridge));
    lay->addWidget(footerItem(QStringLiteral("帧率"), m_footRate));
    lay->addWidget(footerItem(QStringLiteral("丢帧"), m_footLost));
    lay->addWidget(footerItem(QStringLiteral("累计充电"), m_footSessions));
    lay->addWidget(footerItem(QStringLiteral("累计电量"), m_footEnergy));
    lay->addStretch(1);

    m_footLastLog = new QLabel;
    m_footLastLog->setObjectName(QStringLiteral("footer"));
    lay->addWidget(m_footLastLog, 0, Qt::AlignVCenter);
    return footer;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    // 窗口较窄时，底部那行提示会被挤压裁切，直接隐藏更干净
    if (m_footLastLog)
        m_footLastLog->setVisible(width() >= 1180);
}

void MainWindow::setupShortcuts()
{
    new QShortcut(QKeySequence(Qt::Key_F11), this, [this]
                  { isFullScreen() ? showNormal() : showFullScreen(); });
    new QShortcut(QKeySequence(Qt::Key_Escape), this, [this]
                  { showNormal(); });
    // 数字键切换页签
    for (int i = 0; i < 3; ++i)
    {
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)), this,
                      [this, i]
                      { m_tabs->setCurrentIndex(i); });
    }
    // 环回模式开关：没有真实桥时用来演示
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")), this, [this]
                  { setLoopbackEnabled(!m_bridge->isLoopbackEnabled()); });
}

// ------------------------------------------------------------------
// 链路状态与统计
// ------------------------------------------------------------------

void MainWindow::onLinkStateChanged(BridgeClient::LinkState state)
{
    QString text;
    QString level;
    switch (state)
    {
    case BridgeClient::LinkState::Online:
        text = QStringLiteral("设备在线");
        level = QStringLiteral("ok");
        break;
    case BridgeClient::LinkState::BridgeOnly:
        text = QStringLiteral("桥在线 · 设备无数据");
        level = QStringLiteral("warn");
        break;
    case BridgeClient::LinkState::NoBridge:
        text = QStringLiteral("桥未连接");
        level = QStringLiteral("err");
        break;
    }
    setPill(m_pillLink, text, level);

    if (m_bridge->isLoopbackEnabled())
    {
        setPill(m_pillDevice, QStringLiteral("环回仿真数据"), QStringLiteral("warn"));
    }
    else
    {
        setPill(m_pillDevice,
                QStringLiteral("监听 UDP %1（单向接收）").arg(m_bridge->statusPort()),
                state == BridgeClient::LinkState::NoBridge ? QStringLiteral("dim")
                                                           : QStringLiteral("info"));
    }

    // 掉线时在底部给出可操作的提示，而不是只报一个状态码
    if (state == BridgeClient::LinkState::NoBridge && !m_bridge->isLoopbackEnabled())
    {
        setPill(m_pillState, QStringLiteral("请启动 mdc_bridge"), QStringLiteral("err"));
    }
    else
    {
        setPill(m_pillState,
                QStringLiteral("当前状态：%1")
                    .arg(StationController::stateText(m_controller->state())),
                QStringLiteral("info"));
    }
}

void MainWindow::onStatisticsChanged()
{
    m_footSessions->setText(QStringLiteral("%1 次").arg(m_controller->totalSessions()));
    m_footEnergy->setText(QStringLiteral("%1 kWh")
                              .arg(m_controller->totalEnergyKwh(), 0, 'f', 3));
}

// ------------------------------------------------------------------
// 数据刷新
// ------------------------------------------------------------------

void MainWindow::updateClock()
{
    const QDateTime now = QDateTime::currentDateTime();
    static const char *weekNames[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};

    if (m_clockTime)
        m_clockTime->setText(now.toString(QStringLiteral("HH:mm:ss")));
    if (m_clockDate)
        m_clockDate->setText(QStringLiteral("%1  %2")
                                 .arg(now.date().toString(QStringLiteral("yyyy-MM-dd")))
                                 .arg(QString::fromUtf8(weekNames[now.date().dayOfWeek() - 1])));

    // 链路质量指标跟着时钟一起刷新（1Hz 足够）
    if (m_footRate)
        m_footRate->setText(QStringLiteral("%1 Hz").arg(m_bridge->frameRateHz()));
    if (m_footLost)
        m_footLost->setText(QString::number(m_bridge->lostFrames()));
    if (m_footBridge)
    {
        m_footBridge->setText(m_bridge->isLoopbackEnabled()
                                  ? QStringLiteral("环回仿真")
                                  : QStringLiteral("监听 UDP %1")
                                        .arg(m_bridge->statusPort()));
    }
    if (m_pillState)
    {
        setPill(m_pillState,
                QStringLiteral("状态：%1")
                    .arg(StationController::stateText(m_controller->state())),
                QStringLiteral("info"));
    }
}

void MainWindow::setPill(QLabel *pill, const QString &text, const QString &level)
{
    if (!pill)
        return;
    if (pill->text() == text && pill->property("level").toString() == level)
        return;
    pill->setText(text);
    pill->setProperty("level", level);
    Theme::repolish(pill);
}

// ------------------------------------------------------------------
// 日志
// ------------------------------------------------------------------

void MainWindow::onLogMessage(int level, const QString &message)
{
    appendLog(level, message);
}

void MainWindow::appendLog(int level, const QString &message)
{
    if (m_footLastLog)
    {
        m_footLastLog->setText(QStringLiteral("%1  %2")
                                   .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
                                        message));
    }

    if (!m_logList)
        return;

    QColor color = Theme::TextSecondary;
    switch (level)
    {
    case 1:
        color = Theme::Amber;
        break;
    case 2:
        color = Theme::Red;
        break;
    case 3:
        color = Theme::Green;
        break;
    default:
        break;
    }

    auto *item = new QListWidgetItem;
    item->setText(QStringLiteral("%1   %2")
                      .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), message));
    item->setForeground(color);
    item->setToolTip(message);

    m_logList->insertItem(0, item);
    while (m_logList->count() > kMaxLogItems)
        delete m_logList->takeItem(m_logList->count() - 1);
}
