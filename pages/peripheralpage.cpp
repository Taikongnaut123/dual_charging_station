#include "peripheralpage.h"

#include "pages/pageutils.h"
#include "theme.h"
#include "widgets/bytegrid.h"
#include "widgets/card.h"
#include "widgets/levelbar.h"
#include "widgets/lightindicator.h"

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

using namespace PageUtils;

PeripheralPage::PeripheralPage(StationController *controller, QWidget *parent)
    : QWidget(parent), m_ctl(controller)
{
    buildUi();
    connect(m_ctl, &StationController::dataChanged, this, &PeripheralPage::onData);
    onData(m_ctl->data());
}

void PeripheralPage::buildUi()
{
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(12);
    grid->addWidget(buildIrCard(), 0, 0);
    grid->addWidget(buildIoCard(), 0, 1);
    grid->addWidget(buildFanCard(), 1, 0);
    grid->addWidget(buildLedCard(), 1, 1);
    grid->setRowStretch(0, 3);
    grid->setRowStretch(1, 2);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
}

Card *PeripheralPage::buildIrCard()
{
    auto *card = new Card(QStringLiteral("红外与停车检测"));

    m_irLA = new LightIndicator(QStringLiteral("左侧 A 管 ir_l_a"));
    m_irLB = new LightIndicator(QStringLiteral("左侧 B 管 ir_l_b"));
    m_irRA = new LightIndicator(QStringLiteral("右侧 A 管 ir_r_a"));
    m_irRB = new LightIndicator(QStringLiteral("右侧 B 管 ir_r_b"));
    m_irSh = new LightIndicator(QStringLiteral("停车握手 ir_sh"));
    m_irBs = new LightIndicator(QStringLiteral("电池充电中 ir_bs"));
    m_echo1 = new LightIndicator(QStringLiteral("光电输入 echo1"));
    m_seated = new LightIndicator(QStringLiteral("机器人落座 robot_seated"));

    m_seated->setColors(Theme::Green, Theme::Track);
    m_irBs->setColors(Theme::Green, Theme::Track);
    m_irSh->setColors(Theme::Accent, Theme::Track);

    card->addWidget(m_irLA);
    card->addWidget(m_irLB);
    card->addWidget(m_irRA);
    card->addWidget(m_irRB);
    card->addWidget(m_irSh);
    card->addWidget(m_irBs);
    card->addWidget(m_echo1);
    card->addWidget(m_seated);
    card->addWidget(noteLabel(QStringLiteral(
        "停车对位依赖 4 路红外管依次触发；ir_sh=1 表示停车握手成功。")));
    return card;
}

Card *PeripheralPage::buildIoCard()
{
    auto *card = new Card(QStringLiteral("电源输出与开关"));

    m_vo1 = new LightIndicator(QStringLiteral("输出通道 VO1"));
    m_vo2 = new LightIndicator(QStringLiteral("输出通道 VO2"));
    m_vo3 = new LightIndicator(QStringLiteral("输出通道 VO3"));
    m_vo4 = new LightIndicator(QStringLiteral("输出通道 VO4"));
    m_sw1 = new LightIndicator(QStringLiteral("拨码开关 SW1"));
    m_sw2 = new LightIndicator(QStringLiteral("拨码开关 SW2"));
    m_relay1 = new LightIndicator(QStringLiteral("继电器 RELAY1"));
    m_relay2 = new LightIndicator(QStringLiteral("继电器 RELAY2"));

    m_relay1->setColors(Theme::Green, Theme::Track);
    m_relay2->setColors(Theme::Green, Theme::Track);
    m_vo1->setColors(Theme::Amber, Theme::Track);
    m_vo2->setColors(Theme::Amber, Theme::Track);
    m_vo3->setColors(Theme::Amber, Theme::Track);
    m_vo4->setColors(Theme::Amber, Theme::Track);

    card->addWidget(m_vo1);
    card->addWidget(m_vo2);
    card->addWidget(m_vo3);
    card->addWidget(m_vo4);
    card->addWidget(m_sw1);
    card->addWidget(m_sw2);
    card->addWidget(m_relay1);
    card->addWidget(m_relay2);
    card->addWidget(noteLabel(QStringLiteral(
        "电源输出通道的语义为「1=关 0=开」（以接口指南为准，与 srv 内注释相反）。")));
    return card;
}

Card *PeripheralPage::buildFanCard()
{
    auto *card = new Card(QStringLiteral("风扇"));

    m_fan1 = new LevelBar(QStringLiteral("风扇 1"));
    m_fan2 = new LevelBar(QStringLiteral("风扇 2"));
    m_fan3 = new LevelBar(QStringLiteral("风扇 3"));
    m_fan4 = new LevelBar(QStringLiteral("风扇 4"));

    card->addWidget(m_fan1);
    card->addWidget(m_fan2);
    card->addWidget(m_fan3);
    card->addWidget(m_fan4);

    m_fanTemp = valueLabel();
    card->addWidget(kvRow(QStringLiteral("风扇温度"), m_fanTemp));
    return card;
}

Card *PeripheralPage::buildLedCard()
{
    auto *card = new Card(QStringLiteral("LED 灯效"));

    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(4);

    const auto addChannel = [&](int row, const QString &name, ByteGrid *&gridOut, int count)
    {
        auto *caption = sectionLabel(name);
        grid->addWidget(caption, row, 0);

        gridOut = new ByteGrid;
        gridOut->setColumns(count);
        gridOut->setCellSize(24);
        grid->addWidget(gridOut, row + 1, 0);
    };

    addChannel(0, QStringLiteral("通道 1 原始数据"), m_ledCh1, 8);
    addChannel(2, QStringLiteral("通道 2 原始数据"), m_ledCh2, 8);
    addChannel(4, QStringLiteral("通道 3 原始数据"), m_ledCh3, 8);
    addChannel(6, QStringLiteral("RGB 原始数据"), m_ledRgb, 8);

    card->addLayout(grid, 1);

    m_ledBrightness = valueLabel();
    card->addWidget(kvRow(QStringLiteral("亮度 led_brightness"), m_ledBrightness));
    return card;
}

// ------------------------------------------------------------------ 刷新

void PeripheralPage::onData(const MdcData &d)
{
    m_irLA->setOn(d.irLA != 0);
    m_irLB->setOn(d.irLB != 0);
    m_irRA->setOn(d.irRA != 0);
    m_irRB->setOn(d.irRB != 0);
    m_irSh->setOn(d.handshaked());
    m_irBs->setOn(d.charging());
    m_echo1->setOn(d.powerioEcho1 != 0);
    m_seated->setOn(d.seated());

    m_irLA->setValueText(QString::number(d.irLA));
    m_irLB->setValueText(QString::number(d.irLB));
    m_irRA->setValueText(QString::number(d.irRA));
    m_irRB->setValueText(QString::number(d.irRB));
    m_irSh->setValueText(QString::number(d.irSh));
    m_irBs->setValueText(QString::number(d.irBs));
    m_echo1->setValueText(QString::number(d.powerioEcho1));
    m_seated->setValueText(QString::number(d.robotSeated));

    const auto setIo = [](LightIndicator *l, quint8 v)
    {
        l->setOn(v != 0);
        l->setValueText(QString::number(v));
    };
    setIo(m_vo1, d.powerioVo1);
    setIo(m_vo2, d.powerioVo2);
    setIo(m_vo3, d.powerioVo3);
    setIo(m_vo4, d.powerioVo4);
    setIo(m_sw1, d.powerioSw1);
    setIo(m_sw2, d.powerioSw2);
    setIo(m_relay1, d.powerioRelay1);
    setIo(m_relay2, d.powerioRelay2);

    const auto setFan = [](LevelBar *bar, quint8 pwm)
    {
        bar->setValue(pwm);
        bar->setValueText(QStringLiteral("%1 %").arg(pwm));
        bar->setAccent(pwm > 80 ? Theme::Amber : Theme::Accent);
    };
    setFan(m_fan1, d.fan1Pwm);
    setFan(m_fan2, d.fan2Pwm);
    setFan(m_fan3, d.fan3Pwm);
    setFan(m_fan4, d.fan4Pwm);
    m_fanTemp->setText(QStringLiteral("%1 ℃").arg(d.fanTemp));

    m_ledCh1->setBytes(d.ledCh1Data);
    m_ledCh2->setBytes(d.ledCh2Data);
    m_ledCh3->setBytes(d.ledCh3Data);
    m_ledRgb->setBytes(d.ledRgbData);
    m_ledBrightness->setText(QString::number(d.ledBrightness));
}
