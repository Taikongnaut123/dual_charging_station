#include "overviewpage.h"

#include "pages/pageutils.h"
#include "theme.h"
#include "widgets/card.h"
#include "widgets/chargecurvechart.h"
#include "widgets/circulargauge.h"
#include "widgets/metrictile.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

using namespace PageUtils;

namespace
{
    /** mV -> "48.66 V" */
    QString voltText(double v)
    {
        return QStringLiteral("%1 V").arg(v, 0, 'f', 2);
    }

    /** A -> "15.0 A" */
    QString ampText(double a)
    {
        return QStringLiteral("%1 A").arg(a, 0, 'f', 1);
    }
} // namespace

OverviewPage::OverviewPage(StationController *controller, QWidget *parent)
    : QWidget(parent), m_ctl(controller)
{
    buildUi();

    connect(m_ctl, &StationController::dataChanged, this, &OverviewPage::onData);
    connect(m_ctl, &StationController::stateChanged, this, &OverviewPage::onStateChanged);
    connect(m_ctl, &StationController::statisticsChanged,
            this, &OverviewPage::onStatisticsChanged);

    onData(m_ctl->data());
    applyState(m_ctl->state());
    onStatisticsChanged();
}

void OverviewPage::buildUi()
{
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(Theme::px(7));

    grid->addWidget(buildPortCard(true), 0, 0);
    grid->addWidget(buildPortCard(false), 0, 1);
    grid->addWidget(buildSummaryCard(), 1, 0, 1, 2);
    grid->addWidget(buildCurveCard(), 2, 0, 1, 2);

    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(3, 1);
}

Card *OverviewPage::buildPortCard(bool manual)
{
    PortWidgets &w = manual ? m_man : m_auto;

    auto *card = new Card(manual ? QStringLiteral("手动充电口")
                                 : QStringLiteral("自动充电口"));
    w.card = card;
    w.badge = card->setBadge(manual ? QStringLiteral("MANUAL") : QStringLiteral("AUTO"),
                             QStringLiteral("info"));

    // 左：SOC 环形表；右：六项指标
    auto *row = new QHBoxLayout;
    row->setSpacing(Theme::px(18));

    w.gauge = new CircularGauge;
    w.gauge->setFixedSize(Theme::px(150), Theme::px(150));
    w.gauge->setCaption(QStringLiteral("SOC"));
    w.gauge->setUnit(QStringLiteral("%"));
    w.gauge->setAccent(manual ? Theme::Accent : Theme::Violet);
    row->addWidget(w.gauge);

    auto *kv = new QVBoxLayout;
    kv->setSpacing(Theme::px(9));

    w.voltage = plainValue();
    w.chargeCurrent = plainValue();
    w.dischargeCurrent = plainValue();
    w.temp = plainValue();
    w.batteryId = plainValue();
    w.chgSwitch = plainValue();

    kv->addWidget(kvRow(QStringLiteral("总电压"), w.voltage));
    kv->addWidget(kvRow(QStringLiteral("充电电流"), w.chargeCurrent));
    kv->addWidget(kvRow(QStringLiteral("放电电流"), w.dischargeCurrent));
    kv->addWidget(kvRow(QStringLiteral("电池温度"), w.temp));
    kv->addWidget(kvRow(QStringLiteral("电池 ID"), w.batteryId));
    kv->addWidget(kvRow(QStringLiteral("充电开关"), w.chgSwitch));
    kv->addStretch(1);

    row->addLayout(kv, 1);
    card->addLayout(row);

    // 未启用提示：只有口子没就绪时显示。
    // 手动口是有线插接，一个条件；自动口是无线的，要"坐到位 + 贴合"两个条件，
    // 文案在 updatePort() 里按实际情况填，因为两种原因排查方向完全不同。
    w.hint = new QLabel;
    w.hint->setObjectName(QStringLiteral("portHint"));
    w.hint->setWordWrap(true);
    w.hint->setVisible(false);
    card->addWidget(w.hint);

    return card;
}

Card *OverviewPage::buildSummaryCard()
{
    auto *card = new Card(QStringLiteral("整桩概况"));
    card->setHelp(QStringLiteral(
        "本卡数据来自两个字段：\n"
        "· 充电状态 —— charge_state（0 故障 / 1 空闲 / 2 充电中 / 3 充满）\n"
        "· 充电模式 —— charge_mode（0 待机 / 1 手动 / 2 自动）\n\n"
        "下面的「本次充电量」「已充时长」「预计剩余」「累计次数 / 电量」"
        "协议里没有，是本程序在本地统计和估算的。"));

    m_stateText = new QLabel;
    m_stateText->setObjectName(QStringLiteral("bigState"));
    m_stateSub = new QLabel;
    m_stateSub->setObjectName(QStringLiteral("footer"));
    card->addWidget(m_stateText);
    card->addWidget(m_stateSub);

    m_tileMode = new MetricTile(QStringLiteral("充电模式"));
    m_tileEnergy = new MetricTile(QStringLiteral("本次充电量"));
    m_tileElapsed = new MetricTile(QStringLiteral("本次已充时长"));
    m_tileRemain = new MetricTile(QStringLiteral("预计剩余"));
    m_tileSessions = new MetricTile(QStringLiteral("累计次数"));
    m_tileTotalEnergy = new MetricTile(QStringLiteral("累计电量"));

    m_tileEnergy->setUnit(QStringLiteral("kWh"));
    m_tileTotalEnergy->setUnit(QStringLiteral("kWh"));
    // 副标题（“本地统计”“按当前功率估算”）不显示了：字号放大后
    // 每个 tile 多一行就要多占近 20px，六个 tile 排一行把曲线图挤出了屏幕。
    // 这两句信息已经写在标题旁的 “?” 里，不再重复占空间。

    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(Theme::px(10));
    grid->addWidget(m_tileMode, 0, 0);
    grid->addWidget(m_tileEnergy, 0, 1);
    grid->addWidget(m_tileElapsed, 0, 2);
    grid->addWidget(m_tileRemain, 0, 3);
    grid->addWidget(m_tileSessions, 0, 4);
    grid->addWidget(m_tileTotalEnergy, 0, 5);
    for (int i = 0; i < 6; ++i)
        grid->setColumnStretch(i, 1);
    card->addLayout(grid);

    // 这里原本还有一行长脚注（充电模式/状态取值 + “本机统计”说明）。
    // 现在已经有两处覆盖同样的信息：
    //   1) 标题旁的 “?” 悬停说明里写了取值含义；
    //   2) 每个估算值的 tile 上都标了“本地统计/按当前功率估算”。
    // 字号放大后这行要占 2～3 行高，直接去掉，给下面的曲线卡腾地方。
    return card;
}

Card *OverviewPage::buildCurveCard()
{
    auto *card = new Card(QStringLiteral("充电电流曲线"));
    card->setBadge(QStringLiteral("最近 90 秒"), QStringLiteral("info"));

    m_chart = new ChargeCurveChart;
    m_chart->setMinimumHeight(Theme::px(110));
    m_chart->setSeries(QStringLiteral("手动口"), QStringLiteral("A"),
                       QStringLiteral("自动口"), QStringLiteral("A"));
    m_chart->setEmptyText(QStringLiteral("等待数据 ..."));
    card->addWidget(m_chart, 1);
    return card;
}

// ------------------------------------------------------------------ 刷新

void OverviewPage::updatePort(bool manual, const MdcData &d)
{
    PortWidgets &w = manual ? m_man : m_auto;

    // 手动口：有线插接，只看 manual_link_status。
    // 自动口：无线充电，必须"机器人坐到位 + 充电口贴合"两个条件同时成立。
    const bool ready = manual ? d.manualLinked() : d.autoReady();
    const bool charging = manual ? d.manualCharging() : d.autoCharging();
    const bool switchOn = manual ? d.manualSwitchOn() : d.autoSwitchOn();

    // 整卡置灰：QSS 靠动态属性 link 选择，改完必须 repolish 才生效
    w.card->setProperty("link", ready ? QStringLiteral("on") : QStringLiteral("off"));
    Theme::repolish(w.card);
    w.hint->setVisible(!ready);
    w.gauge->setDimmed(!ready);

    if (!ready)
    {
        // 没就绪时把这六个字段清成 "--"：设备可能仍然上报上一块的残留值，
        // 直接显示会让人以为"这个口接着一块 0A 的电池"。
        w.badge->setText(QStringLiteral("未启用"));
        w.badge->setProperty("level", QStringLiteral("dim"));
        Theme::repolish(w.badge);
        w.gauge->setValue(0);
        w.gauge->setFlowing(false);
        for (QLabel *l : {w.voltage, w.chargeCurrent, w.dischargeCurrent,
                          w.temp, w.batteryId, w.chgSwitch})
            l->setText(QStringLiteral("--"));

        // 自动口没就绪有两种截然不同的原因，分开说，别让人去查错方向
        if (manual)
            w.hint->setText(QStringLiteral(
                "手动充电线未连接（manual_link_status = 0），本口未启用"));
        else if (!d.hallTriggered())
            w.hint->setText(QStringLiteral(
                "机器人尚未坐到位（hall_switch = 0），自动充电口未启用"));
        else
            w.hint->setText(QStringLiteral(
                "已坐到位但充电口未贴合（auto_link_status = 0），自动充电口未启用"));
        return;
    }

    w.badge->setText(manual ? QStringLiteral("MANUAL") : QStringLiteral("AUTO"));
    w.badge->setProperty("level", charging ? QStringLiteral("ok") : QStringLiteral("dim"));
    Theme::repolish(w.badge);

    w.gauge->animateTo(manual ? d.manualSoc : d.autoSoc);
    w.gauge->setFlowing(charging && m_ctl->state() != StationController::State::Offline);

    w.voltage->setText(voltText(manual ? d.manualVoltageV() : d.autoVoltageV()));
    w.chargeCurrent->setText(ampText(manual ? d.manualChargeCurrentA()
                                            : d.autoChargeCurrentA()));
    w.dischargeCurrent->setText(ampText(manual ? d.manualDischargeCurrentA()
                                               : d.autoDischargeCurrentA()));
    w.temp->setText(QStringLiteral("%1 ℃").arg(manual ? d.manualTemp : d.autoTemp));
    w.batteryId->setText(QString::number(manual ? d.manualBatteryId : d.autoBatteryId));
    w.chgSwitch->setText(switchOn ? QStringLiteral("开") : QStringLiteral("关"));
    w.chgSwitch->setProperty("level", switchOn ? QStringLiteral("ok") : QStringLiteral("dim"));
    Theme::repolish(w.chgSwitch);
}

void OverviewPage::onData(const MdcData &d)
{
    updatePort(true, d);
    updatePort(false, d);

    // ---- 概况 ----
    m_tileMode->setValue(MdcData::chargeModeText(d.chargeMode));
    m_tileMode->setAccent(d.chargeMode == 0 ? Theme::TextSecondary : Theme::Accent);

    m_tileElapsed->setValue(StationController::formatDuration(m_ctl->sessionSeconds()));
    m_tileEnergy->setValue(QString::number(m_ctl->sessionEnergyKwh(), 'f', 3));

    const int remain = m_ctl->remainingSeconds();
    m_tileRemain->setValue(remain < 0 ? QStringLiteral("--")
                                      : StationController::formatDuration(remain));

    // ---- 曲线 ----
    QVector<ChargeCurveChart::CurveSample> samples;
    samples.reserve(m_ctl->curve().size());
    for (const StationController::CurvePoint &p : m_ctl->curve())
    {
        ChargeCurveChart::CurveSample s;
        s.time = p.time;
        s.y1 = p.manualA;
        s.y2 = p.autoA;
        samples.append(s);
    }
    m_chart->setSamples(samples);
}

void OverviewPage::applyState(StationController::State state)
{
    QColor color = Theme::TextSecondary;
    QString title = StationController::stateText(state);
    QString sub;
    const bool live = (state != StationController::State::Offline);

    switch (state)
    {
    case StationController::State::Offline:
        // 离线时先说清"为什么"：桥没了和设备不上报，排查方向完全不同
        color = Theme::Amber;
        title = QStringLiteral("数据中断");
        sub = m_ctl->offlineDetail();
        if (sub.isEmpty())
            sub = QStringLiteral("没有收到设备数据");
        break;
    case StationController::State::Fault:
        color = Theme::Red;
        sub = QStringLiteral("charge_state = 0，请检查充电桩本体的故障指示");
        break;
    case StationController::State::Idle:
        color = Theme::TextSecondary;
        sub = QStringLiteral("充电桩空闲，等待充电");
        break;
    case StationController::State::Charging:
        color = Theme::Green;
        sub = QStringLiteral("正在充电，充电控制由充电桩主机自主执行");
        break;
    case StationController::State::Full:
        color = Theme::Accent;
        sub = QStringLiteral("本次充电已完成");
        break;
    }

    m_stateText->setText(title);
    m_stateText->setStyleSheet(QStringLiteral("color:%1;").arg(color.name()));
    m_stateSub->setText(sub);

    // 离线时必须停掉环形表的流动动画：否则它会一直转，配合冻结的读数，
    // 看起来像是还在正常充电 —— 这是最误导的一种"假在线"。
    // 同理，未就绪的口也不该转（自动口要坐到位且贴合）。
    const MdcData &d = m_ctl->data();
    m_man.gauge->setFlowing(live && d.manualLinked() && d.manualCharging());
    m_auto.gauge->setFlowing(live && d.autoReady() && d.autoCharging());
}

void OverviewPage::onStateChanged(StationController::State state)
{
    applyState(state);
}

void OverviewPage::onStatisticsChanged()
{
    m_tileSessions->setValue(QString::number(m_ctl->totalSessions()));
    m_tileTotalEnergy->setValue(QString::number(m_ctl->totalEnergyKwh(), 'f', 2));
}
