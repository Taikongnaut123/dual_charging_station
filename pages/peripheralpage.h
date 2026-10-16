#ifndef PERIPHERALPAGE_H
#define PERIPHERALPAGE_H

#include "stationcontroller.h"

#include <QWidget>

class ByteGrid;
class Card;
class LevelBar;
class LightIndicator;
class QLabel;

/**
 * 页签三「外设与灯光」
 *
 * 红外 / 电源输出与开关 / 风扇 / LED —— MdcData 里剩下的所有字段。
 * 全部只读展示，控制集中在「设备控制」页，避免误触。
 */
class PeripheralPage : public QWidget
{
    Q_OBJECT

public:
    explicit PeripheralPage(StationController *controller, QWidget *parent = nullptr);

private slots:
    void onData(const MdcData &d);

private:
    void buildUi();
    Card *buildIrCard();
    Card *buildIoCard();
    Card *buildFanCard();
    Card *buildLedCard();

    StationController *m_ctl = nullptr;

    // 红外
    LightIndicator *m_irLA = nullptr;
    LightIndicator *m_irLB = nullptr;
    LightIndicator *m_irRA = nullptr;
    LightIndicator *m_irRB = nullptr;
    LightIndicator *m_irSh = nullptr;
    LightIndicator *m_irBs = nullptr;
    LightIndicator *m_echo1 = nullptr;
    LightIndicator *m_seated = nullptr;

    // 电源输出与开关
    LightIndicator *m_vo1 = nullptr;
    LightIndicator *m_vo2 = nullptr;
    LightIndicator *m_vo3 = nullptr;
    LightIndicator *m_vo4 = nullptr;
    LightIndicator *m_sw1 = nullptr;
    LightIndicator *m_sw2 = nullptr;
    LightIndicator *m_relay1 = nullptr;
    LightIndicator *m_relay2 = nullptr;

    // 风扇
    LevelBar *m_fan1 = nullptr;
    LevelBar *m_fan2 = nullptr;
    LevelBar *m_fan3 = nullptr;
    LevelBar *m_fan4 = nullptr;
    QLabel *m_fanTemp = nullptr;

    // LED
    ByteGrid *m_ledCh1 = nullptr;
    ByteGrid *m_ledCh2 = nullptr;
    ByteGrid *m_ledCh3 = nullptr;
    ByteGrid *m_ledRgb = nullptr;
    QLabel *m_ledBrightness = nullptr;
};

#endif // PERIPHERALPAGE_H
