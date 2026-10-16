#include "powerpage.h"

#include "pages/pageutils.h"
#include "theme.h"
#include "widgets/card.h"
#include "widgets/levelbar.h"
#include "widgets/metrictile.h"

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QVariant>

using namespace PageUtils;

namespace
{
    /** 把 mA 换算成占额定值的百分比，用于进度条 */
    int percentOf(int milliAmp, double nominalAmp)
    {
        if (nominalAmp <= 0.0)
            return 0;
        const double a = milliAmp / 1000.0;
        return qBound(0, int(a / nominalAmp * 100.0 + 0.5), 100);
    }

    /** 额定电流（A），仅用于画进度条的比例，不影响数值显示 */
    constexpr double kNominalInput = 35.0; // 总输入
    constexpr double kNominalJoint = 5.0;  // 关节电机
    constexpr double kNominalNonIso = 3.0; // 12V 非隔离 DCDC
    constexpr double kNominalIso = 1.0;    // 12V 隔离 DCDC

    QLabel *makeValue(const QString &initial = QStringLiteral("--"))
    {
        return valueLabel(initial);
    }

    QString voltText(qint32 mV)
    {
        return QStringLiteral("%1 V").arg(mV / 1000.0, 0, 'f', 2);
    }

    QString ampText(qint32 mA)
    {
        return QStringLiteral("%1 A").arg(mA / 1000.0, 0, 'f', 2);
    }
} // namespace

PowerPage::PowerPage(StationController *controller, QWidget *parent)
    : QWidget(parent), m_ctl(controller)
{
    buildUi();
    connect(m_ctl, &StationController::dataChanged, this, &PowerPage::onData);
    onData(m_ctl->data());
}

void PowerPage::buildUi()
{
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(12);
    grid->addWidget(buildBatteryCard(), 0, 0);
    grid->addWidget(buildInputCard(), 0, 1);
    grid->addWidget(buildDcdcCard(), 0, 2);
    grid->addWidget(buildJointCard(), 1, 0, 1, 3);
    grid->setRowStretch(2, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
}

Card *PowerPage::buildBatteryCard()
{
    auto *card = new Card(QStringLiteral("电池状态"));
    m_batBadge = card->setBadge(QStringLiteral("--"), QStringLiteral("dim"));

    m_batSoc = makeValue();
    m_batVoltage = makeValue();
    m_batCharge = makeValue();
    m_batDischarge = makeValue();
    m_batTemp = makeValue();

    card->addWidget(kvRow(QStringLiteral("电量 SOC"), m_batSoc));
    card->addWidget(kvRow(QStringLiteral("总电压"), m_batVoltage));
    card->addWidget(kvRow(QStringLiteral("充电电流"), m_batCharge));
    card->addWidget(kvRow(QStringLiteral("放电电流"), m_batDischarge));
    card->addWidget(kvRow(QStringLiteral("电池温度"), m_batTemp));
    card->addWidget(noteLabel(QStringLiteral("battery_total_voltage / charge_current / "
                                             "discharge_current / soc / temp")));
    return card;
}

Card *PowerPage::buildInputCard()
{
    auto *card = new Card(QStringLiteral("总输入与保护"));

    m_inputVoltage = makeValue();
    m_inputCurrent = makeValue();
    m_alarm = makeValue();
    m_emergency = makeValue();

    card->addWidget(kvRow(QStringLiteral("总输入电压"), m_inputVoltage));
    card->addWidget(kvRow(QStringLiteral("总输入电流"), m_inputCurrent));
    m_inputBar = new LevelBar(QStringLiteral("负载率"));
    m_inputBar->setAccent(Theme::Accent);
    card->addWidget(m_inputBar);

    card->addWidget(kvRow(QStringLiteral("报警状态"), m_alarm));
    card->addWidget(kvRow(QStringLiteral("紧急状态"), m_emergency));
    card->addWidget(noteLabel(QStringLiteral("pmc_alarm_status / pmc_emergency_status "
                                             "非 0 即异常")));
    return card;
}

Card *PowerPage::buildJointCard()
{
    auto *card = new Card(QStringLiteral("关节电机供电"));
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(10);

    const auto addJoint = [&](int col, const QString &name,
                              QLabel *&voltOut, LevelBar *&currOut)
    {
        auto *box = new QVBoxLayout;
        box->setSpacing(6);

        box->addWidget(sectionLabel(name));

        voltOut = valueLabel(QStringLiteral("-- V"));
        box->addWidget(kvRow(QStringLiteral("电压"), voltOut));

        currOut = new LevelBar(QStringLiteral("电流"));
        currOut->setAccent(Theme::Violet);
        box->addWidget(currOut);

        auto *holder = new QWidget;
        holder->setLayout(box);
        grid->addWidget(holder, 0, col);
    };

    addJoint(0, QStringLiteral("头部电机"), m_headV, m_headI);
    addJoint(1, QStringLiteral("右腿电机"), m_rightV, m_rightI);
    addJoint(2, QStringLiteral("左腿电机"), m_leftV, m_leftI);

    card->addLayout(grid, 1);
    return card;
}

Card *PowerPage::buildDcdcCard()
{
    auto *card = new Card(QStringLiteral("12V DCDC"));

    m_nonIsoV = makeValue();
    m_nonIsoI = new LevelBar(QStringLiteral("非隔离电流"));
    m_nonIsoI->setAccent(Theme::Accent);
    m_isoI = new LevelBar(QStringLiteral("隔离电流"));
    m_isoI->setAccent(Theme::Amber);

    card->addWidget(kvRow(QStringLiteral("非隔离输出电压"), m_nonIsoV));
    card->addWidget(m_nonIsoI);
    card->addWidget(m_isoI);
    card->addWidget(noteLabel(QStringLiteral("pmc_nonisolated_voltage / "
                                             "pmc_nonisolated_current / pmc_isolated_current")));
    return card;
}

// ------------------------------------------------------------------ 刷新

void PowerPage::onData(const MdcData &d)
{
    m_batSoc->setText(QStringLiteral("%1 %").arg(d.batterySoc));
    m_batVoltage->setText(voltText(d.batteryTotalVoltage));
    m_batCharge->setText(ampText(d.batteryChargeCurrent));
    m_batDischarge->setText(ampText(d.batteryDischargeCurrent));
    m_batTemp->setText(QStringLiteral("%1 ℃").arg(d.batteryTemp));

    const QString batBadge = d.charging() ? QStringLiteral("充电中")
                                          : (d.seated() ? QStringLiteral("已落座")
                                                        : QStringLiteral("待机"));
    if (m_batBadge->text() != batBadge)
    {
        m_batBadge->setText(batBadge);
        m_batBadge->setProperty("level", d.charging() ? QStringLiteral("ok")
                                                      : QStringLiteral("dim"));
        Theme::repolish(m_batBadge);
    }

    m_inputVoltage->setText(voltText(d.pmcTotalVoltage));
    m_inputCurrent->setText(ampText(d.pmcTotalCurrent));
    const int loadPercent = percentOf(d.pmcTotalCurrent, kNominalInput);
    m_inputBar->setValue(loadPercent);
    m_inputBar->setValueText(QStringLiteral("%1 %").arg(loadPercent));

    const auto setStatus = [](QLabel *l, qint32 v)
    {
        l->setText(v == 0 ? QStringLiteral("0 正常") : QStringLiteral("%1 异常").arg(v));
        l->setStyleSheet(QStringLiteral("color:%1;")
                             .arg(v == 0 ? Theme::Green.name() : Theme::Red.name()));
    };
    setStatus(m_alarm, d.pmcAlarmStatus);
    setStatus(m_emergency, d.pmcEmergencyStatus);

    m_headV->setText(voltText(d.pmcHeadVoltage));
    m_headI->setValue(percentOf(d.pmcHeadCurrent, kNominalJoint));
    m_headI->setValueText(ampText(d.pmcHeadCurrent));

    m_rightV->setText(voltText(d.pmcRightVoltage));
    m_rightI->setValue(percentOf(d.pmcRightCurrent, kNominalJoint));
    m_rightI->setValueText(ampText(d.pmcRightCurrent));

    m_leftV->setText(voltText(d.pmcLeftVoltage));
    m_leftI->setValue(percentOf(d.pmcLeftCurrent, kNominalJoint));
    m_leftI->setValueText(ampText(d.pmcLeftCurrent));

    m_nonIsoV->setText(voltText(d.pmcNonisolatedVoltage));
    m_nonIsoI->setValue(percentOf(d.pmcNonisolatedCurrent, kNominalNonIso));
    m_nonIsoI->setValueText(ampText(d.pmcNonisolatedCurrent));

    m_isoI->setValue(percentOf(d.pmcIsolatedCurrent, kNominalIso));
    m_isoI->setValueText(ampText(d.pmcIsolatedCurrent));
}
