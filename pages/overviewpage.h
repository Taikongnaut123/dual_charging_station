#ifndef OVERVIEWPAGE_H
#define OVERVIEWPAGE_H

#include "stationcontroller.h"

#include <QVector>
#include <QWidget>

class Card;
class ChargeCurveChart;
class CircularGauge;
class MetricTile;
class StageTimeline;
class QLabel;
class QProgressBar;
class QPushButton;

/**
 * 页签一「充电总览」
 *
 *   左：电池状态（环形 SOC + 电池四项）
 *   中：充电状态（大字 + 真实流程时间轴 + 本次充电统计 + 输出继电器操作）
 *   右：实时曲线（充电电流 / 电池电压）+ 无线充电状态
 */
class OverviewPage : public QWidget
{
    Q_OBJECT

public:
    explicit OverviewPage(StationController *controller, QWidget *parent = nullptr);

private slots:
    void onData(const MdcData &d);
    void onStateChanged(StationController::State state);
    void onStageChanged();
    void onStatisticsChanged();

private:
    void buildUi();
    Card *buildBatteryCard();
    Card *buildChargingCard();
    Card *buildCurveCard();
    Card *buildWirelessCard();
    void applyState(StationController::State state);

    StationController *m_ctl = nullptr;

    CircularGauge *m_gauge = nullptr;
    ChargeCurveChart *m_chart = nullptr;
    StageTimeline *m_stage = nullptr;
    QProgressBar *m_progress = nullptr;

    QLabel *m_stateText = nullptr;
    QLabel *m_stateSub = nullptr;
    QLabel *m_socText = nullptr;
    QLabel *m_batteryBadge = nullptr;

    QLabel *m_kvVoltage = nullptr;
    QLabel *m_kvChargeCurrent = nullptr;
    QLabel *m_kvDischargeCurrent = nullptr;
    QLabel *m_kvTemp = nullptr;

    MetricTile *m_tileEnergy = nullptr;
    MetricTile *m_tileElapsed = nullptr;
    MetricTile *m_tileRemain = nullptr;
    MetricTile *m_tilePower = nullptr;
    MetricTile *m_tileRelay = nullptr;
    MetricTile *m_tileSeated = nullptr;

    QLabel *m_wchResult = nullptr;
    QLabel *m_wchBusy = nullptr;
    QLabel *m_wchRequest = nullptr;
    QLabel *m_wchTx = nullptr;
    QLabel *m_wchRx = nullptr;
    QLabel *m_wchTick = nullptr;

    QPushButton *m_relayOnBtn = nullptr;
    QPushButton *m_relayOffBtn = nullptr;
};

#endif // OVERVIEWPAGE_H
