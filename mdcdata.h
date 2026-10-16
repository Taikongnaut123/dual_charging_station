#ifndef MDCDATA_H
#define MDCDATA_H

#include "mdc_protocol.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QVector>
#include <QtGlobal>

/**
 * MdcData —— 与下位机 `mdc_serial/msg/MdcData` 一一对应的数据结构。
 *
 * 字段名、顺序、单位与 doc/MdcData.msg 完全一致，**不做任何单位换算**：
 * 电压是 mV、电流是 mA，与网络层和 ROS2 层保持同一套数值，
 * 换算只在界面显示时进行（避免多处换算导致量纲混乱）。
 *
 * 注意：下位机的消息里**没有** SOH、循环次数、充电阶段(CC/CV)、剩余时间，
 * 这些要么是本地统计/估算，要么就不显示。
 */
struct MdcData
{
    // ---------------------------------------------------------- 电池状态
    qint32 batteryTotalVoltage = 0;     // 总电压 mV
    qint32 batteryChargeCurrent = 0;    // 充电电流 mA
    qint32 batteryDischargeCurrent = 0; // 放电电流 mA
    qint32 batterySoc = 0;              // 电量 %
    qint32 batteryTemp = 0;             // 温度 ℃

    // ---------------------------------------------------------- 电源管理 PMC
    qint32 pmcTotalVoltage = 0;       // 总输入电压 mV
    qint32 pmcTotalCurrent = 0;       // 总输入电流 mA
    qint32 pmcHeadVoltage = 0;        // 头部电机电压 mV
    qint32 pmcHeadCurrent = 0;        // 头部电机电流 mA
    qint32 pmcRightVoltage = 0;       // 右腿电机电压 mV
    qint32 pmcRightCurrent = 0;       // 右腿电机电流 mA
    qint32 pmcLeftVoltage = 0;        // 左腿电机电压 mV
    qint32 pmcLeftCurrent = 0;        // 左腿电机电流 mA
    qint32 pmcNonisolatedVoltage = 0; // 12V 非隔离 DCDC 电压 mV
    qint32 pmcNonisolatedCurrent = 0; // 12V 非隔离 DCDC 电流 mA
    qint32 pmcIsolatedCurrent = 0;    // 12V 隔离 DCDC 电流 mA
    qint32 pmcAlarmStatus = 0;        // 报警状态
    qint32 pmcEmergencyStatus = 0;    // 紧急状态

    // ---------------------------------------------------------- 电源 IO
    quint8 powerioVo1 = 0; // 电源输出通道 1
    quint8 powerioVo2 = 0;
    quint8 powerioVo3 = 0;
    quint8 powerioVo4 = 0;
    quint8 powerioSw1 = 0;    // 拨码开关 1
    quint8 powerioSw2 = 0;    // 拨码开关 2
    quint8 powerioRelay1 = 0; // 继电器 1（充电输出）
    quint8 powerioRelay2 = 0; // 继电器 2
    quint8 powerioEcho1 = 0;  // 光电输入 1
    quint8 robotSeated = 0;   // 机器人是否坐下

    // ---------------------------------------------------------- 红外状态
    quint8 irBs = 0; // 电池充电状态 (1:充电中)
    quint8 irLA = 0; // 左侧 A 管
    quint8 irLB = 0; // 左侧 B 管
    quint8 irRA = 0; // 右侧 A 管
    quint8 irRB = 0; // 右侧 B 管
    quint8 irSh = 0; // 停车握手状态 (1:成功)

    // ---------------------------------------------------------- 风扇状态
    quint8 fan1Pwm = 0;
    quint8 fan2Pwm = 0;
    quint8 fan3Pwm = 0;
    quint8 fan4Pwm = 0;
    qint32 fanTemp = 0;

    // ---------------------------------------------------------- LED 状态
    QVector<quint8> ledCh1Data; // uint8[8]
    QVector<quint8> ledCh2Data; // uint8[8]
    QVector<quint8> ledCh3Data; // uint8[8]
    QVector<quint8> ledRgbData; // uint8[16]
    qint32 ledBrightness = 0;

    // ---------------------------------------------------------- 无线充电
    quint8 wchRequestFlag = 0;
    quint8 wchBusy = 0;
    quint8 wchResult = 0; // 0空闲 1处理中 2成功 3失败
    quint8 wchReserved = 0;
    QVector<quint8> wchTxData; // uint8[4]
    QVector<quint8> wchRxData; // uint8[4]
    quint32 wchUpdateTick = 0; // ms

    MdcData() : ledCh1Data(8, 0), ledCh2Data(8, 0), ledCh3Data(8, 0),
                ledRgbData(16, 0), wchTxData(4, 0), wchRxData(4, 0) {}

    // ================================================================ 派生量
    // 下面这些是"显示用"的换算与判定，不是协议字段。

    double voltageV() const { return batteryTotalVoltage / 1000.0; }
    double chargeCurrentA() const { return batteryChargeCurrent / 1000.0; }
    double dischargeCurrentA() const { return batteryDischargeCurrent / 1000.0; }
    /** 充电功率 kW = 电池电压(V) × 充电电流(A) （协议里没有功率字段，本地算） */
    double chargePowerKw() const { return voltageV() * chargeCurrentA() / 1000.0; }

    bool seated() const { return robotSeated != 0; }
    bool handshaked() const { return irSh != 0; }
    bool charging() const { return irBs != 0; }
    bool relayClosed() const { return powerioRelay1 != 0 || powerioRelay2 != 0; }
    bool emergency() const { return pmcEmergencyStatus != 0; }
    bool alarm() const { return pmcAlarmStatus != 0; }
    /** 有任意一路红外管被遮挡 —— 说明车在充电位附近 */
    bool anyIrBlocked() const
    {
        return irLA || irLB || irRA || irRB || powerioEcho1;
    }

    /** 无线充电结果码 -> 中文 */
    static QString wchResultText(quint8 r)
    {
        switch (r)
        {
        case 0:
            return QStringLiteral("空闲");
        case 1:
            return QStringLiteral("处理中");
        case 2:
            return QStringLiteral("成功");
        case 3:
            return QStringLiteral("失败");
        default:
            return QStringLiteral("未知(%1)").arg(r);
        }
    }

    QString wchResultText() const { return wchResultText(wchResult); }

    // ================================================================ 解析
    /** 从桥发来的 JSON 对象解析；缺字段则保持默认值 */
    static MdcData fromJson(const QJsonObject &o)
    {
        MdcData d;
        auto gi = [&o](const char *k)
        { return o.value(QLatin1String(k)).toInt(); };
        auto gb = [&o](const char *k)
        { return quint8(o.value(QLatin1String(k)).toInt()); };

        d.batteryTotalVoltage = gi(mdcproto::kBatVoltage);
        d.batteryChargeCurrent = gi(mdcproto::kBatChargeCurrent);
        d.batteryDischargeCurrent = gi(mdcproto::kBatDischargeCurrent);
        d.batterySoc = gi(mdcproto::kBatSoc);
        d.batteryTemp = gi(mdcproto::kBatTemp);

        d.pmcTotalVoltage = gi(mdcproto::kPmcTotalVoltage);
        d.pmcTotalCurrent = gi(mdcproto::kPmcTotalCurrent);
        d.pmcHeadVoltage = gi(mdcproto::kPmcHeadVoltage);
        d.pmcHeadCurrent = gi(mdcproto::kPmcHeadCurrent);
        d.pmcRightVoltage = gi(mdcproto::kPmcRightVoltage);
        d.pmcRightCurrent = gi(mdcproto::kPmcRightCurrent);
        d.pmcLeftVoltage = gi(mdcproto::kPmcLeftVoltage);
        d.pmcLeftCurrent = gi(mdcproto::kPmcLeftCurrent);
        d.pmcNonisolatedVoltage = gi(mdcproto::kPmcNonisolatedVoltage);
        d.pmcNonisolatedCurrent = gi(mdcproto::kPmcNonisolatedCurrent);
        d.pmcIsolatedCurrent = gi(mdcproto::kPmcIsolatedCurrent);
        d.pmcAlarmStatus = gi(mdcproto::kPmcAlarmStatus);
        d.pmcEmergencyStatus = gi(mdcproto::kPmcEmergencyStatus);

        d.powerioVo1 = gb(mdcproto::kIoVo1);
        d.powerioVo2 = gb(mdcproto::kIoVo2);
        d.powerioVo3 = gb(mdcproto::kIoVo3);
        d.powerioVo4 = gb(mdcproto::kIoVo4);
        d.powerioSw1 = gb(mdcproto::kIoSw1);
        d.powerioSw2 = gb(mdcproto::kIoSw2);
        d.powerioRelay1 = gb(mdcproto::kIoRelay1);
        d.powerioRelay2 = gb(mdcproto::kIoRelay2);
        d.powerioEcho1 = gb(mdcproto::kIoEcho1);
        d.robotSeated = gb(mdcproto::kIoSeated);

        d.irBs = gb(mdcproto::kIrBs);
        d.irLA = gb(mdcproto::kIrLA);
        d.irLB = gb(mdcproto::kIrLB);
        d.irRA = gb(mdcproto::kIrRA);
        d.irRB = gb(mdcproto::kIrRB);
        d.irSh = gb(mdcproto::kIrSh);

        d.fan1Pwm = gb(mdcproto::kFan1);
        d.fan2Pwm = gb(mdcproto::kFan2);
        d.fan3Pwm = gb(mdcproto::kFan3);
        d.fan4Pwm = gb(mdcproto::kFan4);
        d.fanTemp = gi(mdcproto::kFanTemp);

        d.ledCh1Data = toBytes(o, mdcproto::kLedCh1, 8);
        d.ledCh2Data = toBytes(o, mdcproto::kLedCh2, 8);
        d.ledCh3Data = toBytes(o, mdcproto::kLedCh3, 8);
        d.ledRgbData = toBytes(o, mdcproto::kLedRgb, 16);
        d.ledBrightness = gi(mdcproto::kLedBrightness);

        d.wchRequestFlag = gb(mdcproto::kWchRequestFlag);
        d.wchBusy = gb(mdcproto::kWchBusy);
        d.wchResult = gb(mdcproto::kWchResult);
        d.wchReserved = gb(mdcproto::kWchReserved);
        d.wchTxData = toBytes(o, mdcproto::kWchTxData, 4);
        d.wchRxData = toBytes(o, mdcproto::kWchRxData, 4);
        d.wchUpdateTick = quint32(o.value(QLatin1String(mdcproto::kWchUpdateTick)).toDouble());

        return d;
    }

    /** 解析整包 JSON；返回 false 表示不是有效的状态帧 */
    static bool parse(const QByteArray &payload, MdcData &out, qint64 *seq = nullptr)
    {
        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (!doc.isObject())
            return false;

        const QJsonObject o = doc.object();
        if (o.value(QLatin1String(mdcproto::kType)).toString() != QLatin1String(mdcproto::kTypeState))
            return false;

        if (seq)
            *seq = qint64(o.value(QLatin1String(mdcproto::kSeq)).toDouble());

        out = fromJson(o);
        return true;
    }

    /** 字节数组的十六进制显示，例如 "01 02 FF 00" */
    static QString hexString(const QVector<quint8> &bytes)
    {
        QStringList parts;
        parts.reserve(bytes.size());
        for (quint8 b : bytes)
            parts << QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
        return parts.join(QLatin1Char(' '));
    }

private:
    static QVector<quint8> toBytes(const QJsonObject &o, const char *key, int expect)
    {
        QVector<quint8> out;
        out.reserve(expect);
        const QJsonArray arr = o.value(QLatin1String(key)).toArray();
        for (const QJsonValue &v : arr)
            out.append(quint8(v.toInt()));
        while (out.size() < expect)
            out.append(0);
        return out;
    }
};

#endif // MDCDATA_H
