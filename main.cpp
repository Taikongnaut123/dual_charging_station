#include "mainwindow.h"
#include "theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QLocale>
#include <QStyleFactory>
#include <QTranslator>

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

    parser.addOption(fullscreenOption);
    parser.addOption(loopbackOption);
    parser.addOption(hostOption);
    parser.addOption(cmdPortOption);
    parser.addOption(statPortOption);
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

    if (parser.isSet(fullscreenOption))
        window.showFullScreen();
    else
        window.show();

    return app.exec();
}
