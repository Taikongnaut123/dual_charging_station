#include "mainwindow.h"
#include "theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLocale>
#include <QScreen>
#include <QStyleFactory>
#include <QTimer>
#include <QTranslator>

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>

namespace
{
    /**
     * 当前 DISPLAY 能不能用。
     *
     * 一体屏上常见两种情况：
     *   · 根本没起 X，但环境里还残留 DISPLAY=:0（从桌面 shell 继承/默认值）
     *   · 起了 X，但 root 没有 Xauthority，连不上
     * 这里只看“本地 display 的 socket 在不在”，判不了的（远程 host:0）
     * 一律交给 Qt 自己处理。
     */
    bool xDisplayLooksUsable()
    {
        const QByteArray display = qgetenv("DISPLAY");
        if (display.isEmpty())
            return false;

        const int colon = display.indexOf(':');
        if (colon < 0)
            return false;
        if (colon > 0)
            return true; // host:0 这种远程 display，本地判断不了

        // ":0" / ":0.0" -> 查 /tmp/.X11-unix/X0
        const QByteArray num = display.mid(1).split('.').first();
        bool ok = false;
        const int n = num.toInt(&ok);
        if (!ok)
            return false;
        return QFile::exists(QStringLiteral("/tmp/.X11-unix/X%1").arg(n));
    }

    /**
     * 没 X 就自动走 framebuffer。
     *
     * 板子上直接从串口/SSH 启程序时，Qt 的 xcb 插件会因为连不上 :0 直接
     * abort —— 而那块屏就在那儿，只是没跑 X 而已。有 /dev/fb0 就直接用
     * linuxfb，把“能不能跑起来”这件事和环境解耦。
     *
     * 外部显式设了 QT_QPA_PLATFORM 就完全不动。
     */
    void pickPlatformFallback()
    {
        if (!qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
            return;
        if (xDisplayLooksUsable())
            return;
        if (!QFile::exists(QStringLiteral("/dev/fb0")))
            return;

        qputenv("QT_QPA_PLATFORM", "linuxfb");
        // framebuffer 上不需要那个鼠标指针，画面上留个箭头很难看
        if (qEnvironmentVariableIsEmpty("QT_QPA_FB_HIDECURSOR"))
            qputenv("QT_QPA_FB_HIDECURSOR", "1");
        std::fprintf(stderr,
                     "[显示] 没有可用的 X（DISPLAY=%s），改用 framebuffer（linuxfb）\n",
                     qgetenv("DISPLAY").constData());
        std::fflush(stderr);
    }

    /**
     * 当前有哪几路输出是“接着且已启用”的。
     *
     * /sys/class/drm/card0-* 每个子目录对应一路连接器，status 是插没插线，
     * enabled 是内核有没有给它点屏。fbdev 的尺寸问题只在多路同时启用时出现。
     */
    int activeDisplayCount()
    {
        int n = 0;
        const QDir drm(QStringLiteral("/sys/class/drm"));
        const QStringList entries =
            drm.entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
        for (const QString &e : entries)
        {
            if (!e.startsWith(QLatin1String("card")))
                continue;

            QFile status(QStringLiteral("/sys/class/drm/%1/status").arg(e));
            QFile enabled(QStringLiteral("/sys/class/drm/%1/enabled").arg(e));
            if (!status.open(QIODevice::ReadOnly) || !enabled.open(QIODevice::ReadOnly))
                continue;
            if (status.readAll().trimmed() == QByteArrayLiteral("connected")
                && enabled.readAll().trimmed() == QByteArrayLiteral("enabled"))
                ++n;
        }
        return n;
    }

    /**
     * 多路输出共用一块 framebuffer 时，修正内核报出来的假尺寸。
     *
     * RK3588 这类 SoC 的 drm fbdev 只有一块设备，会被所有输出共用，尺寸取
     * 各路输出的并集。现场这块板同时点着 HDMI 和板载 DSI：
     *
     *     # fbset -i
     *     mode "800x768"
     *         geometry 800 768 1024 1280 32
     *         LineLength 4096                ← 每行其实有 1024 像素
     *
     * 800 是 DSI 屏的宽、768 是 HDMI 的高，内核拼出来一个不存在的模式。
     * Qt 的 linuxfb 只看 xres/yres，于是把屏幕当成 800x768，界面铺满 800×768
     * 就收工；而实际每行 1024 像素，右边 224 列一个像素都没写，永远是黑的。
     * 现场看到的就是“右边一条黑边”。
     *
     * 这里在 Qt 加载平台插件之前把 xres 改成真实行宽（xres_virtual），Qt 就能
     * 拿到 1024x768，界面自然铺满。
     *
     * 只在“多路输出同时启用”时才动手：单屏板子 fbdev 尺寸本来就是对的，而有些
     * 单屏板子用 xres_virtual 做 panning，改了反而会把内容推到画面外。
     * 确实需要关掉时，设环境变量 DCS_NO_FB_FIX=1。
     */
    void fixFbdevGeometryIfUnioned()
    {
        if (qEnvironmentVariableIsSet("DCS_NO_FB_FIX"))
            return;
        if (!qgetenv("QT_QPA_PLATFORM").startsWith(QByteArrayLiteral("linuxfb")))
            return;
        if (activeDisplayCount() < 2)
            return;

        const QByteArray dev = qEnvironmentVariableIsEmpty("QT_QPA_FB")
                                   ? QByteArrayLiteral("/dev/fb0")
                                   : qgetenv("QT_QPA_FB");
        const int fd = ::open(dev.constData(), O_RDWR | O_CLOEXEC);
        if (fd < 0)
            return;

        fb_var_screeninfo var {};
        fb_fix_screeninfo fix {};
        const bool queried = ::ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0
                             && ::ioctl(fd, FBIOGET_FSCREENINFO, &fix) == 0;
        if (queried && var.bits_per_pixel > 0 && fix.line_length > 0
            && var.xres_virtual > var.xres
            && fix.line_length * 8u / var.bits_per_pixel == var.xres_virtual)
        {
            const unsigned reported = var.xres;
            fb_var_screeninfo want = var;
            want.xres = var.xres_virtual;
            want.xoffset = 0;
            if (::ioctl(fd, FBIOPUT_VSCREENINFO, &want) == 0)
            {
                std::fprintf(stderr,
                             "[显示] 多路输出共用一块 framebuffer：内核报可见宽度 %u，"
                             "实际行宽 %u，已修正（否则右侧会留一条黑边）\n",
                             reported, unsigned(var.xres_virtual));
            }
            else
            {
                std::fprintf(stderr,
                             "[显示] 多路输出共用一块 framebuffer：可见宽度 %u 小于"
                             "实际行宽 %u，修正失败，右侧可能留黑边\n",
                             reported, unsigned(var.xres_virtual));
            }
            std::fflush(stderr);
        }
        ::close(fd);
    }
} // namespace

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

    // 同样必须早于 QApplication：平台插件是在构造时加载的
    pickPlatformFallback();

    // 也必须在 QApplication 之前：linuxfb 插件在构造时就把 fb 尺寸读走了
    fixFbdevGeometryIfUnioned();

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
        QStringLiteral("强制指定界面缩放系数（含间距、控件尺寸；默认按屏幕分辨率自动计算）"),
        QStringLiteral("scale"));
    QCommandLineOption textScaleOption(
        QStringLiteral("text-scale"),
        QStringLiteral("只放大/缩小字号，不动间距（默认 1.15；嫌字小就调到 1.25）"),
        QStringLiteral("scale"));

    parser.addOption(fullscreenOption);
    parser.addOption(loopbackOption);
    parser.addOption(statPortOption);
    parser.addOption(verboseOption);
    parser.addOption(uiScaleOption);
    parser.addOption(textScaleOption);
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

            // 方向/比例自检。本界面按 1360×800 横屏设计，如果输出被接成了
            // 竖屏（例如 HDMI 模式是 1024×1280 而面板是 1024×768），显示器
            // 会自己缩放，结果是画面被压扁 + 一侧留黑边。这类问题在程序里
            // 修不了，但至少要说出来，别让人以为是界面写错了。
            const qreal screenRatio = avail.width() / qreal(avail.height());
            const qreal designRatio = Theme::kDesignWidth / qreal(Theme::kDesignHeight);
            if (screenRatio < 1.0 || qAbs(screenRatio - designRatio) > designRatio * 0.35)
            {
                std::fprintf(stderr,
                             "[显示] 注意：屏幕 %dx%d（宽高比 %.2f）与设计稿 %dx%d"
                             "（宽高比 %.2f）相差较大。\n"
                             "       如果看到画面被压扁或一侧留黑边，请先把输出模式"
                             "改成面板原生分辨率（这块 8 寸屏是 1024x768@60）：\n"
                             "       video=HDMI-A-1:1024x768@60（内核参数），"
                             "改完用 cat /sys/class/graphics/fb0/virtual_size 确认。\n",
                             avail.width(), avail.height(), double(screenRatio),
                             Theme::kDesignWidth, Theme::kDesignHeight, double(designRatio));
                std::fflush(stderr);
            }
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

    // 字号总闸门。样式表里的字号都是设计稿值，这里一次性放大，
    // 不用去改几十个 font-size。间距不跟着走（那是 geoScale 的事）。
    qreal textBoost = 1.15;
    if (parser.isSet(textScaleOption))
    {
        bool ok = false;
        const qreal v = parser.value(textScaleOption).toDouble(&ok);
        if (ok && v > 0.5 && v <= 2.5)
            textBoost = v;
        else
            qWarning("--text-scale 参数无效，用默认值");
    }
    Theme::setTextBoost(textBoost);

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

    // linuxfb / eglfs 是"裸屏"：没有窗口管理器，也就没有"窗口"这个概念，
    // 不铺满就是四周（或底部）留黑边。这类平台不传 -f 也直接全屏。
    const QString platform = QGuiApplication::platformName();
    const bool bareScreen = (platform == QLatin1String("linuxfb") ||
                             platform == QLatin1String("eglfs") ||
                             platform == QLatin1String("minimalegl"));

    if (parser.isSet(fullscreenOption) || bareScreen)
        window.enterFullScreen();
    else
        window.show();

    return app.exec();
}
