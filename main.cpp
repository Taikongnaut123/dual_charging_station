#include "mainwindow.h"
#include "theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QLocale>
#include <QScreen>
#include <QStyleFactory>
#include <QTimer>
#include <QTranslator>

#include <cmath>
#include <cstdio>

int main(int argc, char *argv[])
{
    // ------------------------------------------------------------------
    // 关掉 Qt 的 xcb-GLX 集成。
    //
    // 工控板/开发板上板卡厂商的 GPU 驱动常常不全（RK3588 上典型），Qt 的 xcb
    // 平台插件启动时会通过 GLX 集成去探测 Mesa 驱动，探测失败就往终端刷：
    //     libGL error: glx: failed to create dri3 screen
    //     libGL error: failed to load driver: rockchip
    // 这些只是噪音：本程序界面全部由 QPainter 光栅绘制，一行 OpenGL 都不用，
    // 探测失败后 Qt 自动走软件路径，界面照常显示。
    // 置成 none 让 xcb 干脆不加载 GLX 集成插件，从根上不产生这些报错。
    //
    // 必须在 QApplication 构造之前 —— 平台插件就是在那时加载的。
    // 只在外部没指定时才设默认值，保留用环境变量改回去的余地。
    // ------------------------------------------------------------------
    if (qEnvironmentVariableIsEmpty("QT_XCB_GL_INTEGRATION"))
        qputenv("QT_XCB_GL_INTEGRATION", "none");

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // 工控屏普遍是高 DPI 小尺寸面板，开启缩放保证界面不糊
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("DualChargingStation"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QApplication::setOrganizationName(QStringLiteral("RobotCharge"));

    // Fusion 风格在各平台下表现一致，配合自定义样式表效果最稳定
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // 字号与样式表在算出屏幕缩放系数后再设置（见下方 parser.process 之后）

    // 多语言：优先加载系统语言对应的翻译文件（:/i18n/...）
    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages)
    {
        const QString baseName = QStringLiteral("dual_charging_station_") + QLocale(locale).name();
        if (translator.load(QStringLiteral(":/i18n/") + baseName))
        {
            app.installTranslator(&translator);
            break;
        }
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("双轮足机器人充电桩显示屏程序（Qt Widgets + mdc_bridge）"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption fullscreenOption(
        {QStringLiteral("f"), QStringLiteral("fullscreen")},
        QStringLiteral("以全屏模式启动（充电桩一体屏的默认运行方式）"));
    QCommandLineOption loopbackOption(
        QStringLiteral("loopback"),
        QStringLiteral("使用内置仿真数据，不连接 mdc_bridge（开发/演示用）"));
    QCommandLineOption statPortOption(
        QStringLiteral("stat-port"),
        QStringLiteral("本程序接收状态帧的 UDP 端口"),
        QStringLiteral("port"),
        QString::number(mdcproto::kDefaultStatPort));
    // 注意：长选项只能用 --verbose。短选项 -v 已被 addVersionOption() 占用。
    QCommandLineOption verboseOption(
        QStringLiteral("verbose"),
        QStringLiteral("把链路状态与运行统计打印到标准输出"
                       "（无头环境 / CI / 现场排障用，需要看日志时加这个参数）"));

    // 现场如果觉得字偏大/偏小，不用改代码重编，直接调这个系数即可
    QCommandLineOption uiScaleOption(
        QStringLiteral("ui-scale"),
        QStringLiteral("强制指定界面缩放系数（默认按屏幕分辨率自动计算）"),
        QStringLiteral("scale"));

    parser.addOption(fullscreenOption);
    parser.addOption(loopbackOption);
    parser.addOption(statPortOption);
    parser.addOption(verboseOption);
    parser.addOption(uiScaleOption);
    parser.process(app);

    // ------------------------------------------------------------------
    // 屏幕适配
    //
    // 界面按 1360x800 的设计稿编写。一体屏是 8 寸 1024x768，比设计稿小
    // 得多，不缩放的话窗口恒为 1360x800，右侧和底部直接跑到屏幕外面。
    //
    // geo  : 几何尺寸系数。宽高各自需要的比例取小值，保证整幅内容放得下
    // font : 字号系数。取 sqrt 折中——8 寸屏像素密度高，字号跟着几何等比
    //        例缩会小到读不清；后果是行高相对变大、总高可能超出页面，
    //        由外面的 QScrollArea 负责滚动，比“字看不清”好。
    // ------------------------------------------------------------------
    qreal geoScale = 1.0;
    if (const QScreen *screen = QGuiApplication::primaryScreen())
    {
        const QSize avail = screen->availableGeometry().size();
        if (avail.width() > 0 && avail.height() > 0)
        {
            geoScale = qMin(avail.width() / qreal(Theme::kDesignWidth),
                            avail.height() / qreal(Theme::kDesignHeight));
            // 下限不能卡得太高：如果板子上的 Qt 开了高 DPI 缩放（8 寸 160 DPI
            // 很容易触发），availableGeometry() 给的是逻辑像素，会比物理像素
            // 小一大截，卡在 0.6 就会溢出屏幕。
            geoScale = qBound(0.35, geoScale, 1.0);
        }
    }
    if (parser.isSet(uiScaleOption))
    {
        bool ok = false;
        const qreal v = parser.value(uiScaleOption).toDouble(&ok);
        if (ok && v > 0.3 && v <= 3.0)
            geoScale = v;
        else
            qWarning("--ui-scale 参数无效，按屏幕自动计算");
    }
    const qreal fontScale = qBound(0.8, std::sqrt(geoScale), 1.0);
    Theme::setUiScale(geoScale, fontScale);

    // 中文界面下默认字号略小一号，具体尺寸由样式表控制
    QFont appFont = app.font();
    appFont.setPointSizeF(10.0 * fontScale);
    app.setFont(appFont);
    app.setStyleSheet(Theme::styleSheet());

    MainWindow window;

    const bool loopback = parser.isSet(loopbackOption);
    if (!loopback)
    {
        const quint16 statPort = parser.value(statPortOption).toUShort();
        if (!window.startBridge(statPort))
        {
            // 端口被占用等情况：退回环回模式，至少让界面可用
            qWarning("无法绑定 UDP %u，已退回环回仿真模式", unsigned(statPort));
            window.setLoopbackEnabled(true);
        }
    }
    else
    {
        window.setLoopbackEnabled(true);
    }

    // ------------------------------------------------------------------
    // --verbose：把链路状态与统计打到标准输出。
    // 一体屏装好之后没有调试器可用，CI 里也没有显示器，只能靠这几行日志
    // 判断"界面是不是真的在收数据"，而不是只判断"进程没崩"。
    //
    // 必须逐行 fflush：标准输出接管道时是全缓冲的，进程被 kill 时缓冲区
    // 里的内容会整体丢掉，CI 里就会看到"什么都没有"。
    // ------------------------------------------------------------------
    if (parser.isSet(verboseOption))
    {
        const auto emitLine = [](const QString &line)
        {
            std::fprintf(stdout, "%s\n", qPrintable(line));
            std::fflush(stdout);
        };

        BridgeClient *client = window.bridgeClient();

        QObject::connect(client, &BridgeClient::linkStateChanged,
                         [emitLine](BridgeClient::LinkState s)
                         {
                             emitLine(QStringLiteral("[链路] %1")
                                          .arg(BridgeClient::linkStateText(s)));
                         });

        QObject::connect(client, &BridgeClient::logMessage,
                         [emitLine](int level, const QString &msg)
                         {
                             emitLine(QStringLiteral("[日志 L%1] %2").arg(level).arg(msg));
                         });

        // 每 2 秒一条统计
        auto *statTimer = new QTimer(&window);
        QObject::connect(statTimer, &QTimer::timeout, [emitLine, client]()
                         {
            const MdcData &d = client->lastData();
            // 没收到数据时不能直接打印字段原始值：charge_state 的默认值是 0，
            // 而 0 的含义恰好是"故障"，会把"没数据"误报成"故障"。
            const bool live = (client->linkState() == BridgeClient::LinkState::Online);
            emitLine(QStringLiteral("[统计] 帧=%1 速率=%2Hz 丢帧=%3 链路=%4 "
                                    "模式=%5 状态=%6 手动SOC=%7 自动SOC=%8")
                         .arg(client->frameCount())
                         .arg(client->frameRateHz())
                         .arg(client->lostFrames())
                         .arg(BridgeClient::linkStateText(client->linkState()))
                         .arg(live ? MdcData::chargeModeText(d.chargeMode)
                                   : QStringLiteral("--"))
                         .arg(live ? MdcData::chargeStateText(d.chargeState)
                                   : QStringLiteral("--"))
                         .arg(live ? QStringLiteral("%1%").arg(d.manualSoc)
                                   : QStringLiteral("--"))
                         .arg(live ? QStringLiteral("%1%").arg(d.autoSoc)
                                   : QStringLiteral("--"))); });
        statTimer->start(2000);
    }

    if (parser.isSet(fullscreenOption))
        window.showFullScreen();
    else
        window.show();

    return app.exec();
}
