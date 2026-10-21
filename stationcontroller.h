#ifndef STATIONCONTROLLER_H
#define STATIONCONTROLLER_H

#include "bridgeclient.h"
#include "mdcdata.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * StationController —— 把设备遥测（MdcData）翻译成界面需要的状态
 *
 * 职责边界：
 *  · 状态派生：由 charge_state / charge_mode / ccu_motor_status 组合出
 *    界面要展示的桩状态与 CCU 电机进度；
 *  · 本地统计：协议里没有累计充电量、次数、剩余时间，这些由本机积分/
 *    估算并持久化，界面上必须标注"本地统计"以示区别；
 *  · 不直接碰网络：全部通过 BridgeClient。
 *
 * 与旧版的区别：新协议是"只收不发"，充电桩主机自主完成充电控制，
 * 所以这里**没有任何下发接口**（旧的继电器/风扇/LED/WCH 控制全部删除）。
 */
class StationController : public QObject
{
    Q_OBJECT

public:
    /** 由 charge_state 直接派生的桩状态 */
    enum class State
    {
        Offline,  // 无数据（桥未连接 / 设备未上报）
        Fault,    // 充电桩故障 (charge_state = 0)
        Idle,     // 空闲 (1)
        Charging, // 充电中 (2)
        Full      // 充满 (3)
    };
    Q_ENUM(State)

    /** 曲线采样点：两个充电口的充电电流（单位 A） */
    struct CurvePoint
    {
        QDateTime time;
        double manualA = 0.0;
        double autoA = 0.0;
    };

    explicit StationController(BridgeClient *client, QObject *parent = nullptr);
    ~StationController() override;

    const MdcData &data() const { return m_data; }
    State state() const { return m_state; }
    bool hasData() const { return m_hasData; }
    BridgeClient *client() const { return m_client; }

    // ---------------------------------------------------------- CCU 电机进度
    /** CCU 电机状态 6 个步骤（0x12 帧里 ccu_motor_status 的 1~6） */
    static QStringList ccuStageLabels();
    int ccuStageCurrent() const { return m_ccuStageCurrent; }
    int ccuStageReached() const { return m_ccuStageReached; }
    bool ccuFaulted() const { return m_ccuFaulted; }

    // ---------------------------------------------------------- 本地统计
    double sessionEnergyKwh() const { return m_sessionEnergyKwh; }
    int sessionSeconds() const { return m_sessionSeconds; }
    /** 预计剩余时间（秒），-1 表示无法估算 */
    int remainingSeconds() const;
    int totalSessions() const { return m_totalSessions; }
    double totalEnergyKwh() const { return m_totalEnergyKwh; }
    /** 仅用于估算剩余时间与充电量，需按实际电池组容量配置 */
    double packCapacityKwh() const { return m_packCapacityKwh; }
    void setPackCapacityKwh(double kwh) { m_packCapacityKwh = kwh; }

    /** 当前在充/已连的口：0=无 1=手动 2=自动 */
    int activePort() const;

    const QVector<CurvePoint> &curve() const { return m_curve; }

    static QString stateText(State s);
    static QString formatDuration(int seconds);

public slots:
    /** 清零累计统计 */
    void resetStatistics();

signals:
    void dataChanged(const MdcData &d);
    void stateChanged(StationController::State s);
    void ccuStageChanged();
    void statisticsChanged();

private slots:
    void onData(const MdcData &d);
    void onLinkStateChanged(BridgeClient::LinkState s);

private:
    void evaluateState(const MdcData &d);
    void updateCcuStage(const MdcData &d);
    void integrate(const MdcData &d);
    void startSession();
    void finishSession();
    void loadStatistics();
    void saveStatistics();

    BridgeClient *m_client = nullptr;

    MdcData m_data;
    State m_state = State::Offline;
    bool m_hasData = false;

    int m_ccuStageCurrent = -1;
    int m_ccuStageReached = -1;
    bool m_ccuFaulted = false;

    // 本地统计
    QElapsedTimer m_clock;
    qint64 m_lastIntegrateMs = 0;
    double m_sessionEnergyKwh = 0.0;
    int m_sessionSeconds = 0;
    double m_sessionSecondsAcc = 0.0; // 秒级累加器（避免 int 取整丢失）
    double m_sessionStartSoc = 0.0;   // 本次充电起始 SOC
    int m_sessionPort = 0;            // 本次充电是哪个口（1手动 2自动）
    double m_totalEnergyKwh = 0.0;
    int m_totalSessions = 0;
    double m_packCapacityKwh = 1.92; // 48V / 40Ah，按实际电池组改

    QVector<CurvePoint> m_curve;
    int m_curveDivider = 0;
};

#endif // STATIONCONTROLLER_H
