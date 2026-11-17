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

    /**
     * 离线原因 —— 区分"桥没了"和"设备没数据"，两者排查方向完全不同：
     * 前者查桥进程/网络，后者查串口/mdc_node/QoS。
     */
    enum class OfflineReason
    {
        None,     // 在线
        NoBridge, // 收不到桥的心跳
        NoDevice  // 桥在线，但设备没上报
    };
    Q_ENUM(OfflineReason)

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
    OfflineReason offlineReason() const { return m_offlineReason; }
    /** 离线的可操作说明（在线时返回空串） */
    QString offlineDetail() const;
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
    /**
     * 预计剩余充电时间（秒），-1 表示无法估算。
     *
     * 由 integrate() 每帧（2Hz）重算并做指数平滑后缓存，这里只读缓存值：
     * 直接按当前电流现算的话，读数会随电流抖动每 0.5 秒跳一次。
     */
    int remainingSeconds() const { return m_remainingSeconds; }
    int totalSessions() const { return m_totalSessions; }
    double totalEnergyKwh() const { return m_totalEnergyKwh; }

    /** 电池组标称容量（Ah），默认 10Ah（CDRX4810，13S2P） */
    double packCapacityAh() const { return m_packCapacityAh; }
    /** 电池组标称电压（V），默认 46.8V（3.6V × 13 串） */
    double packNominalV() const { return m_packNominalV; }
    /** 标称能量（kWh）= Ah × 标称电压 / 1000，默认 0.468 kWh */
    double packCapacityKwh() const { return m_packCapacityAh * m_packNominalV / 1000.0; }
    void setPackCapacityAh(double ah) { m_packCapacityAh = ah; }

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
    /** 按 CC/CV 两段模型估算剩余时间，结果写进 m_remainingSeconds */
    void updateRemainingEstimate(const MdcData &d);
    void startSession();
    void finishSession();
    void loadStatistics();
    void saveStatistics();

    BridgeClient *m_client = nullptr;

    MdcData m_data;
    State m_state = State::Offline;
    bool m_hasData = false;
    OfflineReason m_offlineReason = OfflineReason::None;

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

    // ---- 电池组参数（CDRX4810 规格书 V0.2）----
    // 13S2P / 标称 10Ah / 标称 46.8V / 充电限制 54.6V / 截止 0.02C(200mA)
    double m_packCapacityAh = 10.0; // 标称容量 Ah
    double m_packNominalV = 46.8;   // 标称电压 V（3.6V × 13）
    double m_chargeLimitV = 54.6;   // 充电限制电压 V（4.2V × 13）
    double m_taperCurrentA = 0.2;   // 充电截止电流 A（0.02C）

    // ---- 剩余时间估算 ----
    int m_remainingSeconds = -1;  // 平滑后的剩余时间，-1 = 无法估算
    double m_remainingRaw = -1.0; // 上一帧的原始估算值（平滑用）
    double m_ccCurrentA = 0.0;    // 本次会话观测到的最大充电电流（≈CC 段电流）

    QVector<CurvePoint> m_curve;
    int m_curveDivider = 0;
};

#endif // STATIONCONTROLLER_H
