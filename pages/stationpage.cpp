#include "stationpage.h"

#include "pages/pageutils.h"
#include "theme.h"
#include "widgets/card.h"
#include "widgets/lightindicator.h"
#include "widgets/stagetimeline.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

using namespace PageUtils;

namespace
{
    /** 0x12 帧里 int32 型的开关量：0/1 之外的值原样显示，方便发现异常 */
    QString rawNumber(qint32 v)
    {
        return QString::number(v);
    }
} // namespace

StationPage::StationPage(StationController *controller, QWidget *parent)
    : QWidget(parent), m_ctl(controller)
{
    buildUi();

    connect(m_ctl, &StationController::dataChanged, this, &StationPage::onData);
    connect(m_ctl, &StationController::ccuStageChanged, this, &StationPage::onCcuStageChanged);

    onData(m_ctl->data());
    onCcuStageChanged();
}

void StationPage::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::px(12));

    layout->addWidget(buildCcuCard());
    layout->addWidget(buildIoCard());
    layout->addWidget(buildRawCard(), 1);
}

Card *StationPage::buildCcuCard()
{
    auto *card = new Card(QStringLiteral("CCU 电机状态"));
    card->setBadge(QStringLiteral("ccu_motor_status"), QStringLiteral("info"));

    m_ccuText = new QLabel;
    m_ccuText->setObjectName(QStringLiteral("bigState"));
    card->addWidget(m_ccuText);

    m_stage = new StageTimeline;
    m_stage->setFixedHeight(Theme::px(74));
    m_stage->setLabels(StationController::ccuStageLabels());
    card->addWidget(m_stage);

    m_ccuHint = noteLabel(QString());
    card->addWidget(m_ccuHint);
    return card;
}

Card *StationPage::buildIoCard()
{
    auto *card = new Card(QStringLiteral("连接与充电状态"));
    card->setBadge(QStringLiteral("link / charge / hall / fan"), QStringLiteral("info"));

    auto *row = new QHBoxLayout;
    row->setSpacing(Theme::px(10));

    m_manLink = new LightIndicator(QStringLiteral("手动线连接"));
    m_manCharge = new LightIndicator(QStringLiteral("手动充电"));
    m_autoLink = new LightIndicator(QStringLiteral("自动连接"));
    m_autoCharge = new LightIndicator(QStringLiteral("自动充电"));
    m_hall = new LightIndicator(QStringLiteral("霍尔开关"));
    m_fan = new LightIndicator(QStringLiteral("风扇"));

    const QList<LightIndicator *> lights{m_manLink, m_manCharge, m_autoLink,
                                         m_autoCharge, m_hall, m_fan};
    for (LightIndicator *l : lights)
    {
        l->setColors(Theme::Green, Theme::Track);
        row->addWidget(l, 1);
    }
    card->addLayout(row);

    card->addWidget(noteLabel(QStringLiteral(
        "这些开关量由充电桩主机自主控制，本程序只做显示，不下发任何指令。")));
    return card;
}

Card *StationPage::buildRawCard()
{
    auto *card = new Card(QStringLiteral("原始字段一览"));
    card->setBadge(QStringLiteral("对照 ros2 topic echo 排障"), QStringLiteral("info"));

    m_rawMode = plainValue();
    m_rawState = plainValue();
    m_rawCcu = plainValue();
    m_rawManLink = plainValue();
    m_rawManCharge = plainValue();
    m_rawAutoLink = plainValue();
    m_rawAutoCharge = plainValue();
    m_rawHall = plainValue();
    m_rawFan = plainValue();

    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(6);

    int r = 0;
    grid->addWidget(kvRow(QStringLiteral("charge_mode"), m_rawMode), r, 0);
    grid->addWidget(kvRow(QStringLiteral("charge_state"), m_rawState), r, 1);
    grid->addWidget(kvRow(QStringLiteral("ccu_motor_status"), m_rawCcu), r, 2);
    ++r;
    grid->addWidget(kvRow(QStringLiteral("manual_link_status"), m_rawManLink), r, 0);
    grid->addWidget(kvRow(QStringLiteral("manual_charge_status"), m_rawManCharge), r, 1);
    grid->addWidget(kvRow(QStringLiteral("hall_switch"), m_rawHall), r, 2);
    ++r;
    grid->addWidget(kvRow(QStringLiteral("auto_link_status"), m_rawAutoLink), r, 0);
    grid->addWidget(kvRow(QStringLiteral("auto_charge_status"), m_rawAutoCharge), r, 1);
    grid->addWidget(kvRow(QStringLiteral("fan_status"), m_rawFan), r, 2);
    for (int c = 0; c < 3; ++c)
        grid->setColumnStretch(c, 1);

    card->addLayout(grid);
    card->addWidget(noteLabel(QStringLiteral(
        "这里显示协议原始值，用于和 ros2 topic echo /mdc_data 的输出逐项对照。"
        "ccu_motor_status 的 0xFE=断联、0xFF=错误。")));
    return card;
}

// ------------------------------------------------------------------ 刷新

void StationPage::onData(const MdcData &d)
{
    // ---- 指示灯 ----
    m_manLink->setOn(d.manualLinked());
    m_manLink->setValueText(d.linkText(d.manualLinkStatus));

    m_manCharge->setOn(d.manualCharging());
    m_manCharge->setValueText(d.manualCharging() ? QStringLiteral("充电中")
                                                 : QStringLiteral("未充电"));

    m_autoLink->setOn(d.autoLinked());
    m_autoLink->setValueText(d.linkText(d.autoLinkStatus));

    m_autoCharge->setOn(d.autoCharging());
    m_autoCharge->setValueText(d.autoCharging() ? QStringLiteral("充电中")
                                                : QStringLiteral("未充电"));

    m_hall->setOn(d.hallSwitch != 0);
    m_hall->setValueText(d.hallText(d.hallSwitch));

    m_fan->setOn(d.fanStatus != 0);
    m_fan->setValueText(d.onOffText(d.fanStatus));

    // ---- 原始字段 ----
    m_rawMode->setText(QStringLiteral("%1 (%2)").arg(d.chargeMode).arg(MdcData::chargeModeText(d.chargeMode)));
    m_rawState->setText(QStringLiteral("%1 (%2)").arg(d.chargeState).arg(MdcData::chargeStateText(d.chargeState)));

    m_rawCcu->setText(QStringLiteral("%1 / %2").arg(d.ccuMotorStatus).arg(MdcData::ccuMotorStatusText(d.ccuMotorStatus)));
    m_rawCcu->setStyleSheet(d.ccuFault()
                                ? QStringLiteral("color:%1;").arg(Theme::Red.name())
                                : QString());

    m_rawManLink->setText(rawNumber(d.manualLinkStatus));
    m_rawManCharge->setText(rawNumber(d.manualChargeStatus));
    m_rawAutoLink->setText(rawNumber(d.autoLinkStatus));
    m_rawAutoCharge->setText(rawNumber(d.autoChargeStatus));
    m_rawHall->setText(rawNumber(d.hallSwitch));
    m_rawFan->setText(rawNumber(d.fanStatus));
}

void StationPage::onCcuStageChanged()
{
    const QString status = MdcData::ccuMotorStatusText(m_ctl->data().ccuMotorStatus);
    m_ccuText->setText(status);
    m_ccuText->setStyleSheet(
        QStringLiteral("color:%1;")
            .arg(m_ctl->ccuFaulted() ? Theme::Red.name() : Theme::Accent.name()));

    m_stage->setAccent(m_ctl->ccuFaulted() ? Theme::Red : Theme::Accent);
    m_stage->setProgress(m_ctl->ccuStageCurrent(), m_ctl->ccuStageReached(),
                         m_ctl->ccuFaulted());

    if (m_ctl->ccuFaulted())
        m_ccuHint->setText(QStringLiteral(
            "CCU 报断联(0xFE) 或 错误(0xFF)：检查充电桩主机的通信线与供电。"));
    else if (m_ctl->ccuStageCurrent() < 0)
        m_ccuHint->setText(QStringLiteral("暂无数据。"));
    else
        m_ccuHint->setText(QStringLiteral(
            "初始化 -> 回零 -> 到位 由充电桩主机自主完成，本程序只显示进度。"));
}
