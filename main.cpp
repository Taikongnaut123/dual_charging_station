#include "mainwindow.h"
#include "theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QLocale>
#include <QStyleFactory>
#include <QTimer>
#include <QTranslator>

#include <cstdio>

int main(int argc, char *argv[])
{
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

    // 中文界面下默认字号略小一号，具体尺寸由样式表控制
    QFont appFont = app.font();
    appFont.setPointSizeF(10.0);
    app.setFont(appFont);

    app.setStyleSheet(Theme::styleSheet());

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
    QCommandLineOption hostOption(
        QStringLiteral("bridge-host"),
        QStringLiteral("mdc_bridge 所在地址"),
        QStringLiteral("host"),
        QStringLiteral("127.0.0.1"));
    QCommandLineOption cmdPortOption(
        QStringLiteral("cmd-port"),
        QStringLiteral("mdc_bridge 监听控制指令的 UDP 端口"),
        QStringLiteral("port"),
        QString::number(mdcproto::kDefaultCmdPort));
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

    parser.addOption(fullscreenOption);
    parser.addOption(loopbackOption);
    parser.addOption(hostOption);
    parser.addOption(cmdPortOption);
    parser.addOption(statPortOption);
    parser.addOption(verboseOption);
    parser.process(app);

    MainWindow window;

    const bool loopback = parser.isSet(loopbackOption);
    if (!loopback)
    {
        const quint16 cmdPort = parser.value(cmdPortOption).toUShort();
        const quint16 statPort = parser.value(statPortOption).toUShort();
        if (!window.startBridge(cmdPort, statPort, parser.value(hostOption)))
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
        const auto emitLine = [](const QString &line) {
            std::fprintf(stdout, "%s\n", qPrintable(line));
            std::fflush(stdout);
        };

        BridgeClient *client = window.bridgeClient();

        QObject::connect(client, &BridgeClient::linkStateChanged,
                         [emitLine](BridgeClient::LinkState s) {
                             emitLine(QStringLiteral("[链路] %1")
                                          .arg(BridgeClient::linkStateText(s)));
                         });

        QObject::connect(client, &BridgeClient::commandFinished,
                         [emitLine](int id, const QString &cmd, bool ok, const QString &msg) {
                             emitLine(QStringLiteral("[指令] #%1 %2 -> %3 (%4)")
                                          .arg(id)
                                          .arg(cmd)
                                          .arg(ok ? QStringLiteral("成功") : QStringLiteral("失败"))
                                          .arg(msg));
                         });

        QObject::connect(client, &BridgeClient::logMessage,
                         [emitLine](int level, const QString &msg) {
                             emitLine(QStringLiteral("[日志 L%1] %2").arg(level).arg(msg));
                         });

        // 每 2 秒一条统计
        auto *statTimer = new QTimer(&window);
        QObject::connect(statTimer, &QTimer::timeout, [emitLine, client]() {
            const MdcData &d = client->lastData();
            emitLine(QStringLiteral("[统计] 帧=%1 速率=%2Hz 丢帧=%3 链路=%4 电量=%5%")
                         .arg(client->frameCount())
                         .arg(client->frameRateHz())
                         .arg(client->lostFrames())
                         .arg(BridgeClient::linkStateText(client->linkState()))
                         .arg(d.batterySoc));
        });
        statTimer->start(2000);
    }

    if (parser.isSet(fullscreenOption))
        window.showFullScreen();
    else
        window.show();

    return app.exec();
}
