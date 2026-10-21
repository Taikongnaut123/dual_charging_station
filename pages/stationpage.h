#ifndef STATIONPAGE_H
#define STATIONPAGE_H

#include "stationcontroller.h"

#include <QWidget>

class Card;
class LightIndicator;
class StageTimeline;
class QLabel;

/**
 * 页签二「充电桩状态」
 *
 *   CCU 电机进度（6 步时间轴 + 当前状态）
 *   连接与充电状态（6 个指示灯）
 *   原始字段一览（与 ros2 topic echo 对照排障用）
 */
class StationPage : public QWidget
{
    Q_OBJECT

public:
    explicit StationPage(StationController *controller, QWidget *parent = nullptr);

private slots:
    void onData(const MdcData &d);
    void onCcuStageChanged();

private:
    void buildUi();
    Card *buildCcuCard();
    Card *buildIoCard();
    Card *buildRawCard();

    StationController *m_ctl = nullptr;

    StageTimeline *m_stage = nullptr;
    QLabel *m_ccuText = nullptr;
    QLabel *m_ccuHint = nullptr;

    LightIndicator *m_manLink = nullptr;
    LightIndicator *m_manCharge = nullptr;
    LightIndicator *m_autoLink = nullptr;
    LightIndicator *m_autoCharge = nullptr;
    LightIndicator *m_hall = nullptr;
    LightIndicator *m_fan = nullptr;

    // 原始字段
    QLabel *m_rawMode = nullptr;
    QLabel *m_rawState = nullptr;
    QLabel *m_rawCcu = nullptr;
    QLabel *m_rawManLink = nullptr;
    QLabel *m_rawManCharge = nullptr;
    QLabel *m_rawAutoLink = nullptr;
    QLabel *m_rawAutoCharge = nullptr;
    QLabel *m_rawHall = nullptr;
    QLabel *m_rawFan = nullptr;
};

#endif // STATIONPAGE_H
