#ifndef MDCDATA_H
#define MDCDATA_H

#include "mdc_protocol.h"

#include <QLatin1String>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

/**
 * MdcData —— 与下位机 `mdc_serial/msg/MdcData` 一一对应的数据结构。
 *
 * 字段名、顺序、单位与 doc/MdcData.msg 完全一致，**不做任何单位换算**：
 *   电压 mV、电流 0.1A、温度 ℃、SOC %。
 * 换算只在界面显示时做（见下面的派生量），避免多处换算出量纲混乱。
 *
 * 与 v1（0x11 帧）的区别：
 *   · 从"单个电池组"变成"手动充电口 + 自动充电口"两套独立电池状态
 *   · 删除了 PMC 电源管理、电源 IO、红外、风扇 PWM、LED 原始数据、无线充电
 *   · 新增 CCU 电机状态、充电模式、充电状态、霍尔开关、link/charge 状态
 *   · 电流单位从 mA 变成 0.1A
 *   · 不再有下行控制（协议只收不发），所以这里没有任何"下发"相关的东西
 *
 * 注意 msg 里还有 std_msgs/Header（stamp + frame_id），它属于 ROS2 元数据，
 * 不进入 UDP JSON（见 shared/mdc_protocol.h 的说明）。
 */
struct MdcData
{
    // ------------------------------------------------ 手动充电口（MANUAL_RobotStatus）
    quint8 manualChgSwitch = 0;         // 充电开关 0关 1开
    quint8 manualBatteryId = 0;         // 电池 ID
    quint16 manualTotalVoltage = 0;     // 总电压 mV
    quint16 manualChargeCurrent = 0;    // 充电电流 0.1A
    quint16 manualDischargeCurrent = 0; // 放电电流 0.1A
    quint16 manualSoc = 0;              // 电量 %
    quint16 manualTemp = 0;             // 电池温度 ℃

    // -------------------------------------------------- 自动充电口（AUTO_RobotStatus）
    quint8 autoChgSwitch = 0;
    quint8 autoBatteryId = 0;
    quint16 autoTotalVoltage = 0;
    quint16 autoChargeCurrent = 0;
    quint16 autoDischargeCurrent = 0;
    quint16 autoSoc = 0;
    quint16 autoTemp = 0;

    // ---------------------------------------------------------- 充电桩状态
    quint8 ccuMotorStatus = 0;     // 1初始化中 2初始化完成 3回零中 4回零完成 5到位中 6到位完成 0xFE断联 0xFF错误
    quint8 chargeMode = 0;         // 0待机 1手动 2自动
    quint8 chargeState = 0;        // 0故障 1空闲 2充电中 3充满
    qint32 fanStatus = 0;          // 风扇 0关 1开
    qint32 hallSwitch = 0;         // 霍尔开关 0未触发 1触发
    qint32 manualLinkStatus = 0;   // 手动线连接 0未连接 1已连接
    qint32 manualChargeStatus = 0; // 手动充电 0关 1开
    qint32 autoLinkStatus = 0;     // 自动充电连接 0未连接 1已连接
    qint32 autoChargeStatus = 0;   // 自动充电 0关 1开

    // ================================================================ 派生量
    // 下面这些是"显示用"的换算与判定，不是协议字段。

    // ---- 手动充电口 ----
    double manualVoltageV() const { return manualTotalVoltage / 1000.0; }
    /** 协议单位 0.1A，除以 10 得到 A（150 -> 15.0A） */
    double manualChargeCurrentA() const { return manualChargeCurrent / 10.0; }
    double manualDischargeCurrentA() const { return manualDischargeCurrent / 10.0; }
    /** 充电功率 kW = 电压(V) × 充电电流(A) / 1000（协议里没有功率字段，本地算） */
    double manualChargePowerKw() const { return manualVoltageV() * manualChargeCurrentA() / 1000.0; }
    bool manualCharging() const { return manualChargeStatus != 0; }
    bool manualLinked() const { return manualLinkStatus != 0; }
    bool manualSwitchOn() const { return manualChgSwitch != 0; }

    // ---- 自动充电口 ----
    double autoVoltageV() const { return autoTotalVoltage / 1000.0; }
    double autoChargeCurrentA() const { return autoChargeCurrent / 10.0; }
    double autoDischargeCurrentA() const { return autoDischargeCurrent / 10.0; }
    double autoChargePowerKw() const { return autoVoltageV() * autoChargeCurrentA() / 1000.0; }
    bool autoCharging() const { return autoChargeStatus != 0; }
    bool autoLinked() const { return autoLinkStatus != 0; }
    bool autoSwitchOn() const { return autoChgSwitch != 0; }

    /** 霍尔开关：机器人已经坐到充电位上，腹部充电模块贴上充电口 */
    bool hallTriggered() const { return hallSwitch != 0; }

    /**
     * 自动充电口是否"就绪"（可以开始自动充电）。
     *
     * 自动充电是无线的：机器人走到充电桩 -> 坐下 -> 腹部充电模块与充电口
     * 贴合 -> 自动充电连接建立。所以必须**两个条件同时成立**：
     *   hall_switch      机器人坐到位（霍尔开关触发）
     *   auto_link_status 充电口已贴合、连接已建立
     * 少任何一个都不该亮。手动口是有线插接，只看 manual_link_status。
     */
    bool autoReady() const { return hallTriggered() && autoLinked(); }

    // ---- 整体判定 ----
    /** 任意一个口在充电 */
    bool charging() const { return manualCharging() || autoCharging(); }
    /** 充电桩故障（charge_state = 0） */
    bool fault() const { return chargeState == 0; }
    /** CCU 电机是否处于异常（断联 / 错误） */
    bool ccuFault() const { return ccuMotorStatus == 0xFE || ccuMotorStatus == 0xFF; }

    // ================================================================ 枚举文本
    static QString chargeModeText(quint8 v)
    {
        switch (v)
        {
        case 0:
            return QStringLiteral("待机");
        case 1:
            return QStringLiteral("手动");
        case 2:
            return QStringLiteral("自动");
        default:
            return QStringLiteral("未知(%1)").arg(v);
        }
    }

    static QString chargeStateText(quint8 v)
    {
        switch (v)
        {
        case 0:
            return QStringLiteral("故障");
        case 1:
            return QStringLiteral("空闲");
        case 2:
            return QStringLiteral("充电中");
        case 3:
            return QStringLiteral("充满");
        default:
            return QStringLiteral("未知(%1)").arg(v);
        }
    }

    static QString ccuMotorStatusText(quint8 v)
    {
        switch (v)
        {
        case 1:
            return QStringLiteral("初始化中");
        case 2:
            return QStringLiteral("初始化完成");
        case 3:
            return QStringLiteral("回零中");
        case 4:
            return QStringLiteral("回零完成");
        case 5:
            return QStringLiteral("到位中");
        case 6:
            return QStringLiteral("到位完成");
        case 0xFE:
            return QStringLiteral("断联");
        case 0xFF:
            return QStringLiteral("错误");
        default:
            return QStringLiteral("未知(%1)").arg(v);
        }
    }

    /** 0关 1开 */
    static QString onOffText(qint32 v)
    {
        return v != 0 ? QStringLiteral("开") : QStringLiteral("关");
    }

    /** 0未连接 1已连接 */
    static QString linkText(qint32 v)
    {
        return v != 0 ? QStringLiteral("已连接") : QStringLiteral("未连接");
    }

    /** 0未触发 1触发 */
    static QString hallText(qint32 v)
    {
        return v != 0 ? QStringLiteral("已触发") : QStringLiteral("未触发");
    }

    // ================================================================ 解析
    /** 从桥发来的 JSON 对象解析；缺字段则保持默认值 */
    static MdcData fromJson(const QJsonObject &o)
    {
        MdcData d;
        auto gi = [&o](const char *k)
        { return qint32(o.value(QLatin1String(k)).toInt()); };
        auto g8 = [&o](const char *k)
        { return quint8(o.value(QLatin1String(k)).toInt()); };
        auto g16 = [&o](const char *k)
        { return quint16(o.value(QLatin1String(k)).toInt()); };

        // 手动充电口
        d.manualChgSwitch = g8(mdcproto::kManSwitch);
        d.manualBatteryId = g8(mdcproto::kManBatteryId);
        d.manualTotalVoltage = g16(mdcproto::kManVoltage);
        d.manualChargeCurrent = g16(mdcproto::kManChargeCurrent);
        d.manualDischargeCurrent = g16(mdcproto::kManDischargeCurrent);
        d.manualSoc = g16(mdcproto::kManSoc);
        d.manualTemp = g16(mdcproto::kManTemp);

        // 自动充电口
        d.autoChgSwitch = g8(mdcproto::kAutoSwitch);
        d.autoBatteryId = g8(mdcproto::kAutoBatteryId);
        d.autoTotalVoltage = g16(mdcproto::kAutoVoltage);
        d.autoChargeCurrent = g16(mdcproto::kAutoChargeCurrent);
        d.autoDischargeCurrent = g16(mdcproto::kAutoDischargeCurrent);
        d.autoSoc = g16(mdcproto::kAutoSoc);
        d.autoTemp = g16(mdcproto::kAutoTemp);

        // 充电桩状态
        d.ccuMotorStatus = g8(mdcproto::kCcuMotorStatus);
        d.chargeMode = g8(mdcproto::kChargeMode);
        d.chargeState = g8(mdcproto::kChargeState);
        d.fanStatus = gi(mdcproto::kFanStatus);
        d.hallSwitch = gi(mdcproto::kHallSwitch);
        d.manualLinkStatus = gi(mdcproto::kManualLink);
        d.manualChargeStatus = gi(mdcproto::kManualCharge);
        d.autoLinkStatus = gi(mdcproto::kAutoLink);
        d.autoChargeStatus = gi(mdcproto::kAutoCharge);

        return d;
    }
};

#endif // MDCDATA_H
