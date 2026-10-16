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
 *  · 状态派生：MdcData 里没有"充电中/已落座"这种现成状态，要由 ir_bs、
 *    ir_sh、robot_seated、继电器、紧急/报警等字段组合判断出来；
 *  · 本地统计：协议里没有累计充电量、次数、剩余时间，这些由本机积分/
 *    估算并持久化，界面上必须标注"本地统计"以示区别；
 *  · 不直接碰网络：全部通过 BridgeClient。
 *
 * 替代了旧版的 ChargerController —— 旧版那套 CC/CV 充电阶段是仿真臆造的，
 * 真实协议里并不存在，已删除。
 */
class StationController : public QObject
{
    Q_OBJECT

public:
    /** 由 MdcData 若干字段组合派生出的桩状态 */
    enum class State
    {
        Offline,    // 无数据（桥未连接 / 设备未上报）
        Waiting,    // 无车
        Detecting,  // 停车检测中
        Handshaked, // 停车握手成功，等待落座
        Ready,      // 已落座，等待充电
        Charging,   // 充电中
        Completed,  // 本次充电完成
        Alarm,      // 报警（pmc_alarm_status）
        Emergency   // 紧急（pmc_emergency_status）
    };
    Q_ENUM(State)

    /** 曲线采样点 */
    struct CurvePoint
    {
        QDateTime time;
        double currentA = 0.0; // 充电电流 A
        double voltageV = 0.0; // 电池电压 V
    };

    explicit StationController(BridgeClient *client, QObject *parent = nullptr);
    ~StationController() override;

    const MdcData &data() const { return m_data; }
    State state() const { return m_state; }
    bool hasData() const { return m_hasData; }
    BridgeClient *client() const { return m_client; }

    // ---------------------------------------------------------- 阶段时间轴
    /** 真实流程的 5 个步骤（不是协议字段，由红外/落座/充电位组合判断） */
    static QStringList stageLabels();
    int stageCurrent() const { return m_stageCurrent; }
    int stageReached() const { return m_stageReached; }
    bool stageFaulted() const { return m_stageFaulted; }

    // ---------------------------------------------------------- 本地统计
    double sessionEnergyKwh() const { return m_sessionEnergyKwh; }
    int sessionSeconds() const { return m_sessionSeconds; }
    /** 预计剩余时间（秒），-1 表示无法估算 */
    int remainingSeconds() const;
    int totalSessions() const { return m_totalSessions; }
    double totalEnergyKwh() const { return m_totalEnergyKwh; }
    /** 仅用于估算剩余时间，需按实际电池组容量配置 */
    double packCapacityKwh() const { return m_packCapacityKwh; }
    void setPackCapacityKwh(double kwh) { m_packCapacityKwh = kwh; }

    const QVector<CurvePoint> &curve() const { return m_curve; }

    static QString stateText(State s);
    static QString formatDuration(int seconds);

public slots:
    /** 闭合充电输出继电器（relay1+relay2） */
    void closeOutputRelay();
    /** 断开充电输出继电器 */
    void openOutputRelay();
    /** 清零累计统计 */
    void resetStatistics();

signals:
    void dataChanged(const MdcData &d);
    void stateChanged(StationController::State s);
    /** 阶段时间轴发生变化（progress/reached/faulted 任一） */
    void stageChanged();
    void statisticsChanged();

private slots:
    void onData(const MdcData &d);
    void onLinkStateChanged(BridgeClient::LinkState s);

private:
    void evaluateState(const MdcData &d);
    void updateStage(const MdcData &d);
    void integrate(const MdcData &d);
    void startSession();
    void finishSession();
    void loadStatistics();
    void saveStatistics();

    BridgeClient *m_client = nullptr;

    MdcData m_data;
    State m_state = State::Offline;
    bool m_hasData = false;

    int m_stageCurrent = -1;
    int m_stageReached = -1;
    bool m_stageFaulted = false;
    bool m_hadChargeThisVisit = false;

    // 本地统计
    QElapsedTimer m_clock;
    qint64 m_lastIntegrateMs = 0;
    double m_sessionEnergyKwh = 0.0;
    int m_sessionSeconds = 0;
    double m_sessionSecondsAcc = 0.0; // 秒级累加器（避免 int 取整丢失）
    double m_sessionStartSoc = 0.0;   // 本次充电起始 SOC
    double m_totalEnergyKwh = 0.0;
    int m_totalSessions = 0;
    double m_packCapacityKwh = 1.92; // 48V / 40Ah，按实际电池组改

    QVector<CurvePoint> m_curve;
    int m_curveDivider = 0;
};

#endif // STATIONCONTROLLER_H
