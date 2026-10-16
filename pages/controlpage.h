#ifndef CONTROLPAGE_H
#define CONTROLPAGE_H

#include "stationcontroller.h"

#include <QWidget>

class Card;
class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;

/**
 * 页签四「设备控制」—— 对应 MDC 的 5 个 Service
 *
 *   继电器   /mdc/relay_ctrl
 *   无线充电 /mdc/wireless_charge
 *   风扇     /mdc/fan_ctrl
 *   电源输出 /mdc/powerio_ctrl
 *   LED      /mdc/led_ctrl
 *
 * 设计取向：每个卡片都是「先改控件、再点下发」，不做即时下发。
 * 工控屏误触代价高，宁可多点一下也不要手一滑就把输出切了。
 * 下发结果由 mdc_bridge 返回的 ack 回显在卡片底部。
 */
class ControlPage : public QWidget
{
    Q_OBJECT

public:
    explicit ControlPage(StationController *controller, QWidget *parent = nullptr);

private slots:
    void onData(const MdcData &d);
    void onCommandFinished(int id, const QString &cmd, bool ok, const QString &message);

private:
    void buildUi();
    Card *buildRelayCard();
    Card *buildWirelessCard();
    Card *buildFanCard();
    Card *buildPowerioCard();
    Card *buildLedCard();
    void setResult(QLabel *label, bool ok, const QString &message);

    StationController *m_ctl = nullptr;

    // 继电器
    QCheckBox *m_relay1 = nullptr;
    QCheckBox *m_relay2 = nullptr;
    QLabel *m_relayResult = nullptr;

    // 无线充电
    QSpinBox *m_wchByte[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel *m_wchResult = nullptr;
    QLabel *m_wchState = nullptr;

    // 风扇
    QSlider *m_fanSlider[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel *m_fanValue[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel *m_fanResult = nullptr;

    // 电源输出
    QCheckBox *m_vo[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel *m_powerioResult = nullptr;

    // LED
    QComboBox *m_ledMode[3] = {nullptr, nullptr, nullptr};
    QSlider *m_ledDuty[3] = {nullptr, nullptr, nullptr};
    QLabel *m_ledDutyValue[3] = {nullptr, nullptr, nullptr};
    QComboBox *m_rgbMode = nullptr;
    QSlider *m_rgbSlider[3] = {nullptr, nullptr, nullptr};
    QLabel *m_rgbValue[3] = {nullptr, nullptr, nullptr};
    QLabel *m_ledResult = nullptr;
};

#endif // CONTROLPAGE_H
