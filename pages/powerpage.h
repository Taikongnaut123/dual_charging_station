#ifndef POWERPAGE_H
#define POWERPAGE_H

#include "stationcontroller.h"

#include <QWidget>

class Card;
class LevelBar;
class QLabel;

/**
 * 页签二「电源系统」
 *
 * 展示 MdcData 里 PMC 那一整组 13 个字段 + 电池 5 个字段。
 * 电压电流都带进度条，便于一眼看出哪一路负载重。
 */
class PowerPage : public QWidget
{
    Q_OBJECT

public:
    explicit PowerPage(StationController *controller, QWidget *parent = nullptr);

private slots:
    void onData(const MdcData &d);

private:
    void buildUi();
    Card *buildBatteryCard();
    Card *buildInputCard();
    Card *buildJointCard();
    Card *buildDcdcCard();

    StationController *m_ctl = nullptr;

    QLabel *m_batBadge = nullptr;
    QLabel *m_batVoltage = nullptr;
    QLabel *m_batCharge = nullptr;
    QLabel *m_batDischarge = nullptr;
    QLabel *m_batSoc = nullptr;
    QLabel *m_batTemp = nullptr;

    QLabel *m_inputVoltage = nullptr;
    QLabel *m_inputCurrent = nullptr;
    QLabel *m_alarm = nullptr;
    QLabel *m_emergency = nullptr;
    LevelBar *m_inputBar = nullptr;

    QLabel *m_headV = nullptr;
    LevelBar *m_headI = nullptr;
    QLabel *m_rightV = nullptr;
    LevelBar *m_rightI = nullptr;
    QLabel *m_leftV = nullptr;
    LevelBar *m_leftI = nullptr;

    QLabel *m_nonIsoV = nullptr;
    LevelBar *m_nonIsoI = nullptr;
    LevelBar *m_isoI = nullptr;
};

#endif // POWERPAGE_H
