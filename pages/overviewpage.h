#ifndef OVERVIEWPAGE_H
#define OVERVIEWPAGE_H

#include "stationcontroller.h"

#include <QWidget>

class Card;
class ChargeCurveChart;
class CircularGauge;
class MetricTile;
class QLabel;

/**
 * 页签一「充电总览」
 *
 *   上排：手动充电口 | 自动充电口（各含 SOC 环 + 6 项指标）
 *   中排：整桩概况（状态大字 + 充电模式 / 本次充电量 / 已充时长 / 预计剩余 / 累计）
 *   下排：实时曲线（两个口的充电电流）
 */
class OverviewPage : public QWidget
{
    Q_OBJECT

public:
    explicit OverviewPage(StationController *controller, QWidget *parent = nullptr);

private slots:
    void onData(const MdcData &d);
    void onStateChanged(StationController::State state);
    void onStatisticsChanged();

private:
    void buildUi();
    Card *buildPortCard(bool manual);
    Card *buildSummaryCard();
    Card *buildCurveCard();
    void applyState(StationController::State state);

    /** 刷新一个充电口：数值 + 启用/未启用外观（手动/自动共用一份逻辑） */
    void updatePort(bool manual, const MdcData &d);

    StationController *m_ctl = nullptr;

    /** 一个充电口的控件集合 */
    struct PortWidgets
    {
        Card *card = nullptr; // 整卡，置灰靠它的动态属性 link=on/off
        CircularGauge *gauge = nullptr;
        QLabel *badge = nullptr;
        QLabel *hint = nullptr; // 未启用提示，仅 link_status=0 时显示
        QLabel *voltage = nullptr;
        QLabel *chargeCurrent = nullptr;
        QLabel *dischargeCurrent = nullptr;
        QLabel *temp = nullptr;
        QLabel *batteryId = nullptr;
        QLabel *chgSwitch = nullptr;
    };

    PortWidgets m_man;
    PortWidgets m_auto;

    QLabel *m_stateText = nullptr;
    QLabel *m_stateSub = nullptr;

    MetricTile *m_tileMode = nullptr;
    MetricTile *m_tileEnergy = nullptr;
    MetricTile *m_tileElapsed = nullptr;
    MetricTile *m_tileRemain = nullptr;
    MetricTile *m_tileSessions = nullptr;
    MetricTile *m_tileTotalEnergy = nullptr;

    ChargeCurveChart *m_chart = nullptr;
};

#endif // OVERVIEWPAGE_H
