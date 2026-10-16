#include "overviewpage.h"

#include "theme.h"
#include "widgets/card.h"
#include "widgets/chargecurvechart.h"
#include "widgets/circulargauge.h"
#include "widgets/metrictile.h"
#include "widgets/stagetimeline.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>

namespace
{
    /** 所有页面共用的「键 —— 值」行 */
    QWidget *kvRow(const QString &key, QLabel *value)
    {
        auto *row = new QWidget;
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(8);

        auto *k = new QLabel(key);
        k->setObjectName(QStringLiteral("rowKey"));
        lay->addWidget(k, 0, Qt::AlignVCenter);
        lay->addStretch(1);
        lay->addWidget(value, 0, Qt::AlignVCenter);
        return row;
    }

    QLabel *valueLabel()
    {
        auto *l = new QLabel(QStringLiteral("--"));
        l->setObjectName(QStringLiteral("rowVal"));
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return l;
    }
} // namespace

OverviewPage::OverviewPage(StationController *controller, QWidget *parent)
    : QWidget(parent), m_ctl(controller)
{
    buildUi();

    connect(m_ctl, &StationController::dataChanged, this, &OverviewPage::onData);
    connect(m_ctl, &StationController::stateChanged, this, &OverviewPage::onStateChanged);
    connect(m_ctl, &StationController::stageChanged, this, &OverviewPage::onStageChanged);
    connect(m_ctl, &StationController::statisticsChanged,
            this, &OverviewPage::onStatisticsChanged);

    applyState(m_ctl->state());
    onData(m_ctl->data());
    onStageChanged();
}

void OverviewPage::buildUi()
{
    auto *leftCol = new QVBoxLayout;
    leftCol->setContentsMargins(0, 0, 0, 0);
    leftCol->addWidget(buildBatteryCard(), 1);
    auto *left = new QWidget;
    left->setLayout(leftCol);
    left->setMinimumWidth(252);
    left->setMaximumWidth(320);

    auto *midCol = new QVBoxLayout;
    midCol->setContentsMargins(0, 0, 0, 0);
    midCol->addWidget(buildChargingCard(), 1);
    auto *mid = new QWidget;
    mid->setLayout(midCol);
    mid->setMinimumWidth(400);

    auto *rightCol = new QVBoxLayout;
    rightCol->setContentsMargins(0, 0, 0, 0);
    rightCol->setSpacing(12);
    rightCol->addWidget(buildCurveCard(), 4);
    rightCol->addWidget(buildWirelessCard(), 3);
    auto *right = new QWidget;
    right->setLayout(rightCol);
    right->setMinimumWidth(300);
    right->setMaximumWidth(430);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);
    root->addWidget(left, 0);
    root->addWidget(mid, 1);
    root->addWidget(right, 0);
}

Card *OverviewPage::buildBatteryCard()
{
    auto *card = new Card(QStringLiteral("电池状态"));
    m_batteryBadge = card->setBadge(QStringLiteral("等待数据"), QStringLiteral("dim"));

    m_gauge = new CircularGauge;
    m_gauge->setCaption(QStringLiteral("剩余电量 SOC"));
    m_gauge->setSubCaption(QStringLiteral("来自 battery_soc"));
    card->addWidget(m_gauge, 1);

    m_kvVoltage = valueLabel();
    m_kvChargeCurrent = valueLabel();
    m_kvDischargeCurrent = valueLabel();
    m_kvTemp = valueLabel();

    card->addWidget(kvRow(QStringLiteral("总电压"), m_kvVoltage));
    card->addWidget(kvRow(QStringLiteral("充电电流"), m_kvChargeCurrent));
    card->addWidget(kvRow(QStringLiteral("放电电流"), m_kvDischargeCurrent));
    card->addWidget(kvRow(QStringLiteral("电池温度"), m_kvTemp));
    return card;
}

Card *OverviewPage::buildChargingCard()
{
    auto *card = new Card(QStringLiteral("充电状态"));

    // ---- 状态大字 ----
    auto *heroBox = new QVBoxLayout;
    heroBox->setSpacing(2);
    m_stateText = new QLabel(QStringLiteral("无数据"));
    m_stateText->setObjectName(QStringLiteral("heroState"));
    m_stateSub = new QLabel(QStringLiteral("等待 mdc_bridge 上报 /mdc_data"));
    m_stateSub->setObjectName(QStringLiteral("heroSub"));
    m_stateSub->setWordWrap(true);
    heroBox->addWidget(m_stateText);
    heroBox->addWidget(m_stateSub);

    m_socText = new QLabel(QStringLiteral("--"));
    m_socText->setObjectName(QStringLiteral("heroState"));
    m_socText->setAlignment(Qt::AlignRight | Qt::AlignBottom);

    auto *hero = new QHBoxLayout;
    hero->setSpacing(10);
    hero->addLayout(heroBox, 1);
    hero->addWidget(m_socText, 0);
    card->addLayout(hero);

    m_progress = new QProgressBar;
    m_progress->setObjectName(QStringLiteral("chargeBar"));
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(22);
    card->addWidget(m_progress);

    card->contentLayout()->addSpacing(6);

    // ---- 流程时间轴 ----
    auto *stageLabel = new QLabel(QStringLiteral("充电流程"));
    stageLabel->setObjectName(QStringLiteral("sectionLabel"));
    card->addWidget(stageLabel);

    m_stage = new StageTimeline;
    m_stage->setLabels(StationController::stageLabels());
    card->addWidget(m_stage);

    card->contentLayout()->addSpacing(4);

    // ---- 本次充电（本地统计） ----
    m_tileEnergy = new MetricTile(QStringLiteral("本次已充电量"));
    m_tileEnergy->setUnit(QStringLiteral("kWh"));
    m_tileElapsed = new MetricTile(QStringLiteral("已充时长"));
    m_tileRemain = new MetricTile(QStringLiteral("预计剩余"));

    auto *row1 = new QHBoxLayout;
    row1->setSpacing(10);
    row1->addWidget(m_tileEnergy, 1);
    row1->addWidget(m_tileElapsed, 1);
    row1->addWidget(m_tileRemain, 1);
    card->addLayout(row1);

    // ---- 实时状态 ----
    m_tilePower = new MetricTile(QStringLiteral("充电功率"));
    m_tilePower->setUnit(QStringLiteral("kW"));
    m_tileRelay = new MetricTile(QStringLiteral("输出继电器"));
    m_tileSeated = new MetricTile(QStringLiteral("机器人落座"));

    auto *row2 = new QHBoxLayout;
    row2->setSpacing(10);
    row2->addWidget(m_tilePower, 1);
    row2->addWidget(m_tileRelay, 1);
    row2->addWidget(m_tileSeated, 1);
    card->addLayout(row2);

    card->contentLayout()->addSpacing(4);
    card->contentLayout()->addStretch(1);

    // ---- 操作 ----
    m_relayOnBtn = new QPushButton(QStringLiteral("闭合输出继电器"));
    m_relayOnBtn->setObjectName(QStringLiteral("primaryBtn"));
    m_relayOnBtn->setCursor(Qt::PointingHandCursor);
    m_relayOnBtn->setToolTip(QStringLiteral("relay_ctrl { relay1: 1, relay2: 1 }"));
    m_relayOffBtn = new QPushButton(QStringLiteral("断开输出继电器"));
    m_relayOffBtn->setObjectName(QStringLiteral("dangerBtn"));
    m_relayOffBtn->setCursor(Qt::PointingHandCursor);
    m_relayOffBtn->setToolTip(QStringLiteral("relay_ctrl { relay1: 0, relay2: 0 }"));

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(10);
    btnRow->addWidget(m_relayOnBtn, 1);
    btnRow->addWidget(m_relayOffBtn, 1);
    card->addLayout(btnRow);

    connect(m_relayOnBtn, &QPushButton::clicked,
            m_ctl, &StationController::closeOutputRelay);
    connect(m_relayOffBtn, &QPushButton::clicked,
            m_ctl, &StationController::openOutputRelay);
    return card;
}

Card *OverviewPage::buildCurveCard()
{
    auto *card = new Card(QStringLiteral("实时曲线"));
    m_chart = new ChargeCurveChart;
    m_chart->setSeries(QStringLiteral("充电电流"), QStringLiteral("A"),
                       QStringLiteral("电池电压"), QStringLiteral("V"));
    m_chart->setEmptyText(QStringLiteral("尚未采集到数据…"));
    card->addWidget(m_chart, 1);
    return card;
}

Card *OverviewPage::buildWirelessCard()
{
    auto *card = new Card(QStringLiteral("无线充电"));
    m_wchResult = card->setBadge(QStringLiteral("空闲"), QStringLiteral("dim"));

    m_wchBusy = valueLabel();
    m_wchRequest = valueLabel();
    m_wchTx = valueLabel();
    m_wchRx = valueLabel();
    m_wchTick = valueLabel();

    card->addWidget(kvRow(QStringLiteral("请求标志 request"), m_wchRequest));
    card->addWidget(kvRow(QStringLiteral("忙标志 busy"), m_wchBusy));
    card->addWidget(kvRow(QStringLiteral("下发载荷 tx"), m_wchTx));
    card->addWidget(kvRow(QStringLiteral("应答载荷 rx"), m_wchRx));
    card->addWidget(kvRow(QStringLiteral("结果更新 tick"), m_wchTick));

    auto *note = new QLabel(QStringLiteral(
        "提示：一次只下发一条，待结果跳变为 2(成功) 或 3(失败) 后再发下一条。"));
    note->setObjectName(QStringLiteral("footer"));
    note->setWordWrap(true);
    card->addWidget(note);
    return card;
}

// ------------------------------------------------------------------ 刷新

void OverviewPage::onData(const MdcData &d)
{
    m_gauge->animateTo(d.batterySoc);
    m_gauge->setSubCaption(QStringLiteral("总电压 %1 V · 温度 %2 ℃")
                               .arg(d.voltageV(), 0, 'f', 1)
                               .arg(d.batteryTemp));

    m_socText->setText(QStringLiteral("%1%").arg(d.batterySoc));
    m_progress->setValue(qBound(0, int(d.batterySoc), 100));

    m_kvVoltage->setText(QStringLiteral("%1 V").arg(d.voltageV(), 0, 'f', 1));
    m_kvChargeCurrent->setText(QStringLiteral("%1 A").arg(d.chargeCurrentA(), 0, 'f', 2));
    m_kvDischargeCurrent->setText(QStringLiteral("%1 A").arg(d.dischargeCurrentA(), 0, 'f', 2));
    m_kvTemp->setText(QStringLiteral("%1 ℃").arg(d.batteryTemp));

    // ---- 本地统计 ----
    m_tileEnergy->setValue(QString::number(m_ctl->sessionEnergyKwh(), 'f', 3));
    m_tileElapsed->setValue(StationController::formatDuration(m_ctl->sessionSeconds()));
    m_tileRemain->setValue(StationController::formatDuration(m_ctl->remainingSeconds()));

    // ---- 实时状态 ----
    m_tilePower->setValue(QString::number(d.chargePowerKw(), 'f', 3));
    m_tileRelay->setValue(d.relayClosed() ? QStringLiteral("已闭合") : QStringLiteral("已断开"));
    m_tileRelay->setAccent(d.relayClosed() ? Theme::Green : Theme::TextSecondary);
    m_tileSeated->setValue(d.seated() ? QStringLiteral("已落座") : QStringLiteral("未落座"));
    m_tileSeated->setAccent(d.seated() ? Theme::Green : Theme::TextSecondary);

    // ---- 无线充电 ----
    const quint8 r = d.wchResult;
    const QString level = (r == 2)   ? QStringLiteral("ok")
                          : (r == 3) ? QStringLiteral("err")
                          : (r == 1) ? QStringLiteral("warn")
                                     : QStringLiteral("dim");
    if (m_wchResult->text() != d.wchResultText() || m_wchResult->property("level").toString() != level)
    {
        m_wchResult->setText(d.wchResultText());
        m_wchResult->setProperty("level", level);
        Theme::repolish(m_wchResult);
    }
    m_wchBusy->setText(d.wchBusy ? QStringLiteral("1 忙") : QStringLiteral("0 空闲"));
    m_wchRequest->setText(d.wchRequestFlag ? QStringLiteral("1 有请求")
                                           : QStringLiteral("0 无请求"));
    m_wchTx->setText(MdcData::hexString(d.wchTxData));
    m_wchRx->setText(MdcData::hexString(d.wchRxData));
    m_wchTick->setText(QStringLiteral("%1 ms").arg(d.wchUpdateTick));

    // ---- 曲线 ----
    QVector<ChargeCurveChart::CurveSample> samples;
    const QVector<StationController::CurvePoint> &src = m_ctl->curve();
    samples.reserve(src.size());
    for (const StationController::CurvePoint &p : src)
    {
        ChargeCurveChart::CurveSample s;
        s.time = p.time;
        s.y1 = p.currentA;
        s.y2 = p.voltageV;
        samples.append(s);
    }
    m_chart->setSamples(samples);
}

void OverviewPage::onStateChanged(StationController::State state)
{
    applyState(state);
}

void OverviewPage::onStageChanged()
{
    m_stage->setProgress(m_ctl->stageCurrent(), m_ctl->stageReached(),
                         m_ctl->stageFaulted());
}

void OverviewPage::onStatisticsChanged()
{
    m_tileEnergy->setValue(QString::number(m_ctl->sessionEnergyKwh(), 'f', 3));
}

void OverviewPage::applyState(StationController::State state)
{
    const MdcData &d = m_ctl->data();

    QColor accent = Theme::TextMuted;
    QString sub;
    QString badge;
    QString badgeLevel = QStringLiteral("dim");

    switch (state)
    {
    case StationController::State::Offline:
        sub = QStringLiteral("未收到设备数据：请确认 mdc_node 与 mdc_bridge 已运行");
        badge = QStringLiteral("无数据");
        break;
    case StationController::State::Waiting:
        accent = Theme::TextSecondary;
        sub = QStringLiteral("充电位空闲，等待机器人驶入");
        badge = QStringLiteral("等待中");
        break;
    case StationController::State::Detecting:
        accent = Theme::Accent;
        sub = QStringLiteral("红外检测到物体，正在确认停车位置");
        badge = QStringLiteral("检测中");
        badgeLevel = QStringLiteral("info");
        break;
    case StationController::State::Handshaked:
        accent = Theme::Accent;
        sub = QStringLiteral("停车握手成功 (ir_sh=1)，等待机器人落座");
        badge = QStringLiteral("已握手");
        badgeLevel = QStringLiteral("info");
        break;
    case StationController::State::Ready:
        accent = Theme::Accent;
        sub = QStringLiteral("机器人已落座 (robot_seated=1)，可以开始充电");
        badge = QStringLiteral("已就位");
        badgeLevel = QStringLiteral("info");
        break;
    case StationController::State::Charging:
        accent = Theme::Green;
        sub = QStringLiteral("充电中：%1 · 无线 %2")
                  .arg(d.relayClosed() ? QStringLiteral("继电器已闭合")
                                       : QStringLiteral("继电器未闭合"),
                       d.wchResultText());
        badge = QStringLiteral("充电中");
        badgeLevel = QStringLiteral("ok");
        break;
    case StationController::State::Completed:
        accent = Theme::Green;
        sub = QStringLiteral("本次充电完成，可解锁离桩");
        badge = QStringLiteral("已完成");
        badgeLevel = QStringLiteral("ok");
        break;
    case StationController::State::Alarm:
        accent = Theme::Amber;
        sub = QStringLiteral("PMC 报警：pmc_alarm_status = %1").arg(d.pmcAlarmStatus);
        badge = QStringLiteral("报警");
        badgeLevel = QStringLiteral("warn");
        break;
    case StationController::State::Emergency:
        accent = Theme::Red;
        sub = QStringLiteral("PMC 紧急状态：pmc_emergency_status = %1，请立即检查")
                  .arg(d.pmcEmergencyStatus);
        badge = QStringLiteral("紧急");
        badgeLevel = QStringLiteral("err");
        break;
    }

    m_stateText->setText(StationController::stateText(state));
    m_stateText->setStyleSheet(QStringLiteral("color:%1;").arg(accent.name()));
    m_stateSub->setText(sub);
    m_socText->setStyleSheet(QStringLiteral("color:%1;").arg(accent.name()));

    QString progLevel = QStringLiteral("normal");
    if (state == StationController::State::Emergency || state == StationController::State::Alarm)
        progLevel = QStringLiteral("err");
    if (m_progress->property("level").toString() != progLevel)
    {
        m_progress->setProperty("level", progLevel);
        Theme::repolish(m_progress);
    }

    if (m_batteryBadge->text() != badge || m_batteryBadge->property("level").toString() != badgeLevel)
    {
        m_batteryBadge->setText(badge);
        m_batteryBadge->setProperty("level", badgeLevel);
        Theme::repolish(m_batteryBadge);
    }

    m_gauge->setAccent(accent);
    m_gauge->setFlowing(state == StationController::State::Charging);
    m_stage->setAccent(accent);

    // 继电器操作要防误触：只在设备在线时可用
    m_relayOnBtn->setEnabled(state != StationController::State::Offline);
    m_relayOffBtn->setEnabled(state != StationController::State::Offline);
}
