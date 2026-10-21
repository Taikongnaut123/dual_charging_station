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
    grid->setSpacing(12);

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
    w.badge = card->setBadge(manual ? QStringLiteral("MANUAL") : QStringLiteral("AUTO"),
                             QStringLiteral("info"));

    // 左：SOC 环形表；右：六项指标
    auto *row = new QHBoxLayout;
    row->setSpacing(14);

    w.gauge = new CircularGauge;
    w.gauge->setFixedSize(150, 150);
    w.gauge->setCaption(QStringLiteral("SOC"));
    w.gauge->setUnit(QStringLiteral("%"));
    w.gauge->setAccent(manual ? Theme::Accent : Theme::Violet);
    row->addWidget(w.gauge);

    auto *kv = new QVBoxLayout;
    kv->setSpacing(6);

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
    return card;
}

Card *OverviewPage::buildSummaryCard()
{
    auto *card = new Card(QStringLiteral("整桩概况"));
    card->setBadge(QStringLiteral("charge_state / charge_mode"), QStringLiteral("info"));

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
    m_tileEnergy->setSubText(QStringLiteral("本地统计"));
    m_tileTotalEnergy->setSubText(QStringLiteral("本地统计"));
    m_tileRemain->setSubText(QStringLiteral("按当前功率估算"));

    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(10);
    grid->addWidget(m_tileMode, 0, 0);
    grid->addWidget(m_tileEnergy, 0, 1);
    grid->addWidget(m_tileElapsed, 0, 2);
    grid->addWidget(m_tileRemain, 0, 3);
    grid->addWidget(m_tileSessions, 0, 4);
    grid->addWidget(m_tileTotalEnergy, 0, 5);
    for (int i = 0; i < 6; ++i)
        grid->setColumnStretch(i, 1);
    card->addLayout(grid);

    card->addWidget(noteLabel(QStringLiteral(
        "充电模式 0待机/1手动/2自动；充电状态 0故障/1空闲/2充电中/3充满。"
        "「本次」「累计」与预计剩余均为本机统计/估算，协议里没有这些字段。")));
    return card;
}

Card *OverviewPage::buildCurveCard()
{
    auto *card = new Card(QStringLiteral("充电电流曲线"));
    card->setBadge(QStringLiteral("最近 90 秒"), QStringLiteral("info"));

    m_chart = new ChargeCurveChart;
    m_chart->setMinimumHeight(210);
    m_chart->setSeries(QStringLiteral("手动口"), QStringLiteral("A"),
                       QStringLiteral("自动口"), QStringLiteral("A"));
    m_chart->setEmptyText(QStringLiteral("等待数据 ..."));
    card->addWidget(m_chart, 1);
    return card;
}

// ------------------------------------------------------------------ 刷新

void OverviewPage::onData(const MdcData &d)
{
    // ---- 手动充电口 ----
    m_man.gauge->animateTo(d.manualSoc);
    m_man.gauge->setFlowing(d.manualCharging());
    m_man.voltage->setText(voltText(d.manualVoltageV()));
    m_man.chargeCurrent->setText(ampText(d.manualChargeCurrentA()));
    m_man.dischargeCurrent->setText(ampText(d.manualDischargeCurrentA()));
    m_man.temp->setText(QStringLiteral("%1 ℃").arg(d.manualTemp));
    m_man.batteryId->setText(QString::number(d.manualBatteryId));
    m_man.chgSwitch->setText(d.manualSwitchOn() ? QStringLiteral("开") : QStringLiteral("关"));
    m_man.chgSwitch->setProperty("level", d.manualSwitchOn() ? QStringLiteral("ok")
                                                             : QStringLiteral("dim"));
    m_man.badge->setProperty("level", d.manualCharging() ? QStringLiteral("ok")
                                                         : QStringLiteral("dim"));

    // ---- 自动充电口 ----
    m_auto.gauge->animateTo(d.autoSoc);
    m_auto.gauge->setFlowing(d.autoCharging());
    m_auto.voltage->setText(voltText(d.autoVoltageV()));
    m_auto.chargeCurrent->setText(ampText(d.autoChargeCurrentA()));
    m_auto.dischargeCurrent->setText(ampText(d.autoDischargeCurrentA()));
    m_auto.temp->setText(QStringLiteral("%1 ℃").arg(d.autoTemp));
    m_auto.batteryId->setText(QString::number(d.autoBatteryId));
    m_auto.chgSwitch->setText(d.autoSwitchOn() ? QStringLiteral("开") : QStringLiteral("关"));
    m_auto.chgSwitch->setProperty("level", d.autoSwitchOn() ? QStringLiteral("ok")
                                                            : QStringLiteral("dim"));
    m_auto.badge->setProperty("level", d.autoCharging() ? QStringLiteral("ok")
                                                        : QStringLiteral("dim"));

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
    m_man.gauge->setFlowing(live && m_ctl->data().manualCharging());
    m_auto.gauge->setFlowing(live && m_ctl->data().autoCharging());
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
