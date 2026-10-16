#include "controlpage.h"

#include "bridgeclient.h"
#include "pages/pageutils.h"
#include "theme.h"
#include "widgets/card.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace PageUtils;

namespace
{
    /** 一行：名称 + 滑块 + 当前值 */
    QWidget *sliderRow(const QString &name, int max, QSlider *&sliderOut, QLabel *&valueOut,
                       int initial = 0)
    {
        auto *row = new QWidget;
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(10);

        auto *caption = new QLabel(name);
        caption->setObjectName(QStringLiteral("rowKey"));
        caption->setMinimumWidth(72);

        sliderOut = new QSlider(Qt::Horizontal);
        sliderOut->setRange(0, max);
        sliderOut->setValue(initial);

        valueOut = valueLabel(QString::number(initial));
        valueOut->setMinimumWidth(34);

        lay->addWidget(caption);
        lay->addWidget(sliderOut, 1);
        lay->addWidget(valueOut);

        QObject::connect(sliderOut, &QSlider::valueChanged, valueOut,
                         [valueOut](int v)
                         { valueOut->setText(QString::number(v)); });
        return row;
    }

    QComboBox *modeCombo()
    {
        auto *combo = new QComboBox;
        combo->addItem(QStringLiteral("常亮"), 0);
        combo->addItem(QStringLiteral("快闪"), 1);
        combo->addItem(QStringLiteral("慢闪"), 2);
        combo->addItem(QStringLiteral("呼吸"), 3);
        combo->addItem(QStringLiteral("RGB 快闪"), 4);
        combo->addItem(QStringLiteral("RGB 慢闪"), 5);
        return combo;
    }
} // namespace

ControlPage::ControlPage(StationController *controller, QWidget *parent)
    : QWidget(parent), m_ctl(controller)
{
    buildUi();

    connect(m_ctl->client(), &BridgeClient::commandFinished,
            this, &ControlPage::onCommandFinished);
    connect(m_ctl, &StationController::dataChanged, this, &ControlPage::onData);

    // 关键状态要让操作者看得到当前值，避免"闭着眼下发"
    onData(m_ctl->data());
}

void ControlPage::buildUi()
{
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(12);
    grid->addWidget(buildRelayCard(), 0, 0);
    grid->addWidget(buildWirelessCard(), 0, 1);
    grid->addWidget(buildFanCard(), 0, 2);
    grid->addWidget(buildPowerioCard(), 1, 0, 1, 2);
    grid->addWidget(buildLedCard(), 1, 2);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setRowStretch(2, 1);
}

Card *ControlPage::buildRelayCard()
{
    auto *card = new Card(QStringLiteral("输出继电器"));
    card->setBadge(QStringLiteral("relay_ctrl"), QStringLiteral("info"));

    m_relay1 = new QCheckBox(QStringLiteral("继电器 1 吸合"));
    m_relay2 = new QCheckBox(QStringLiteral("继电器 2 吸合"));
    card->addWidget(m_relay1);
    card->addWidget(m_relay2);

    auto *row = new QHBoxLayout;
    row->setSpacing(10);

    auto *apply = new QPushButton(QStringLiteral("下发"));
    apply->setObjectName(QStringLiteral("primaryBtn"));
    auto *allOn = new QPushButton(QStringLiteral("全部吸合"));
    allOn->setObjectName(QStringLiteral("ghostBtn"));
    auto *allOff = new QPushButton(QStringLiteral("全部释放"));
    allOff->setObjectName(QStringLiteral("ghostBtn"));

    row->addWidget(apply, 2);
    row->addWidget(allOn, 2);
    row->addWidget(allOff, 2);
    card->addLayout(row);

    connect(apply, &QPushButton::clicked, this, [this]
            { m_ctl->client()->sendRelay(m_relay1->isChecked() ? 1 : 0,
                                         m_relay2->isChecked() ? 1 : 0); });
    connect(allOn, &QPushButton::clicked, this, [this]
            {
        m_relay1->setChecked(true);
        m_relay2->setChecked(true);
        m_ctl->client()->sendRelay(1, 1); });
    connect(allOff, &QPushButton::clicked, this, [this]
            {
        m_relay1->setChecked(false);
        m_relay2->setChecked(false);
        m_ctl->client()->sendRelay(0, 0); });

    m_relayResult = noteLabel(QStringLiteral("尚未下发指令"));
    card->addWidget(m_relayResult);
    card->addWidget(noteLabel(QStringLiteral(
        "注意：哪一路继电器驱动充电输出需要硬件确认，本页只做透传控制。")));
    return card;
}

Card *ControlPage::buildWirelessCard()
{
    auto *card = new Card(QStringLiteral("无线充电"));
    card->setBadge(QStringLiteral("wireless_charge"), QStringLiteral("info"));

    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    for (int i = 0; i < 4; ++i)
    {
        auto *box = new QVBoxLayout;
        box->setSpacing(2);

        auto *caption = new QLabel(QStringLiteral("字节 %1").arg(i + 1));
        caption->setObjectName(QStringLiteral("tileCaption"));

        m_wchByte[i] = new QSpinBox;
        m_wchByte[i]->setRange(0, 255);
        m_wchByte[i]->setValue(1);
        m_wchByte[i]->setDisplayIntegerBase(16);
        m_wchByte[i]->setPrefix(QStringLiteral("0x"));

        box->addWidget(caption);
        box->addWidget(m_wchByte[i]);
        row->addLayout(box, 1);
    }
    card->addLayout(row);

    auto *apply = new QPushButton(QStringLiteral("下发无线充电载荷"));
    apply->setObjectName(QStringLiteral("primaryBtn"));
    card->addWidget(apply);

    connect(apply, &QPushButton::clicked, this, [this]
            {
        QVector<quint8> payload;
        payload.reserve(4);
        for (int i = 0; i < 4; ++i)
            payload.append(quint8(m_wchByte[i]->value()));
        m_ctl->client()->sendWirelessCharge(payload); });

    m_wchState = valueLabel();
    card->addWidget(kvRow(QStringLiteral("当前结果 wch_result"), m_wchState));
    m_wchResult = noteLabel(QStringLiteral("尚未下发指令"));
    card->addWidget(m_wchResult);
    card->addWidget(noteLabel(QStringLiteral(
        "一次只下发一条；待结果跳变为 2(成功) 或 3(失败) 后再发下一条，"
        "超时门限内不要重发。避免使用 0xFFFFFFFF。")));
    return card;
}

Card *ControlPage::buildFanCard()
{
    auto *card = new Card(QStringLiteral("风扇"));
    card->setBadge(QStringLiteral("fan_ctrl"), QStringLiteral("info"));

    for (int i = 0; i < 4; ++i)
    {
        card->addWidget(sliderRow(QStringLiteral("风扇 %1").arg(i + 1), 100,
                                  m_fanSlider[i], m_fanValue[i], 0));
    }

    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    auto *apply = new QPushButton(QStringLiteral("下发"));
    apply->setObjectName(QStringLiteral("primaryBtn"));
    auto *allOff = new QPushButton(QStringLiteral("全部停转"));
    allOff->setObjectName(QStringLiteral("ghostBtn"));
    row->addWidget(apply, 2);
    row->addWidget(allOff, 1);
    card->addLayout(row);

    connect(apply, &QPushButton::clicked, this, [this]
            { m_ctl->client()->sendFan(quint8(m_fanSlider[0]->value()), quint8(m_fanSlider[1]->value()),
                                       quint8(m_fanSlider[2]->value()), quint8(m_fanSlider[3]->value())); });
    connect(allOff, &QPushButton::clicked, this, [this]
            {
        for (int i = 0; i < 4; ++i)
            m_fanSlider[i]->setValue(0);
        m_ctl->client()->sendFan(0, 0, 0, 0); });

    m_fanResult = noteLabel(QStringLiteral("尚未下发指令"));
    card->addWidget(m_fanResult);
    return card;
}

Card *ControlPage::buildPowerioCard()
{
    auto *card = new Card(QStringLiteral("电源输出通道"));
    card->setBadge(QStringLiteral("powerio_ctrl"), QStringLiteral("info"));

    auto *row = new QHBoxLayout;
    row->setSpacing(14);
    for (int i = 0; i < 4; ++i)
    {
        m_vo[i] = new QCheckBox(QStringLiteral("VO%1 开").arg(i + 1));
        row->addWidget(m_vo[i], 1);
    }
    card->addLayout(row);

    auto *apply = new QPushButton(QStringLiteral("下发"));
    apply->setObjectName(QStringLiteral("primaryBtn"));
    card->addWidget(apply);

    connect(apply, &QPushButton::clicked, this, [this]
            {
        // 位掩码里 1=关 0=开，所以勾选（开）时该位清零
        quint8 mask = 0;
        for (int i = 0; i < 4; ++i) {
            if (!m_vo[i]->isChecked())
                mask |= quint8(1u << i);
        }
        m_ctl->client()->sendPowerio(mask); });

    m_powerioResult = noteLabel(QStringLiteral("尚未下发指令"));
    card->addWidget(m_powerioResult);
    card->addWidget(noteLabel(QStringLiteral(
        "位掩码 bit0~bit3 对应 VO1~VO4，1=关、0=开（与 srv 内注释相反，以接口指南为准）。")));
    return card;
}

Card *ControlPage::buildLedCard()
{
    auto *card = new Card(QStringLiteral("LED 灯效"));
    card->setBadge(QStringLiteral("led_ctrl"), QStringLiteral("info"));

    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(6);

    for (int i = 0; i < 3; ++i)
    {
        auto *caption = sectionLabel(QStringLiteral("通道 %1").arg(i + 1));
        grid->addWidget(caption, i * 2, 0, 1, 2);

        m_ledMode[i] = modeCombo();
        grid->addWidget(m_ledMode[i], i * 2 + 1, 0);

        grid->addWidget(sliderRow(QStringLiteral("亮度"), 100,
                                  m_ledDuty[i], m_ledDutyValue[i], 60),
                        i * 2 + 1, 1);
    }

    const int rgbRow = 6;
    grid->addWidget(sectionLabel(QStringLiteral("RGB")), rgbRow, 0, 1, 2);

    m_rgbMode = modeCombo();
    grid->addWidget(m_rgbMode, rgbRow + 1, 0);

    auto *rgbBox = new QVBoxLayout;
    rgbBox->setSpacing(3);
    const QStringList rgbNames{QStringLiteral("红"), QStringLiteral("绿"),
                               QStringLiteral("蓝")};
    for (int i = 0; i < 3; ++i)
    {
        rgbBox->addWidget(sliderRow(rgbNames.at(i), 100,
                                    m_rgbSlider[i], m_rgbValue[i], 0));
    }
    grid->addLayout(rgbBox, rgbRow + 1, 1);

    card->addLayout(grid, 1);

    auto *apply = new QPushButton(QStringLiteral("下发灯效"));
    apply->setObjectName(QStringLiteral("primaryBtn"));
    card->addWidget(apply);

    connect(apply, &QPushButton::clicked, this, [this]
            { m_ctl->client()->sendLed(
                  quint8(m_ledMode[0]->currentData().toInt()), quint8(m_ledDuty[0]->value()),
                  quint8(m_ledMode[1]->currentData().toInt()), quint8(m_ledDuty[1]->value()),
                  quint8(m_ledMode[2]->currentData().toInt()), quint8(m_ledDuty[2]->value()),
                  quint8(m_rgbMode->currentData().toInt()),
                  quint8(m_rgbSlider[0]->value()), quint8(m_rgbSlider[1]->value()),
                  quint8(m_rgbSlider[2]->value())); });

    m_ledResult = noteLabel(QStringLiteral("尚未下发指令"));
    card->addWidget(m_ledResult);
    return card;
}

// ------------------------------------------------------------------ 刷新

void ControlPage::onData(const MdcData &d)
{
    if (m_wchState)
        m_wchState->setText(QStringLiteral("%1 (%2)")
                                .arg(d.wchResultText())
                                .arg(d.wchResult));
}

void ControlPage::onCommandFinished(int id, const QString &cmd, bool ok, const QString &message)
{
    Q_UNUSED(id)

    QLabel *target = nullptr;
    if (cmd == QLatin1String(mdcproto::kCmdRelay))
        target = m_relayResult;
    else if (cmd == QLatin1String(mdcproto::kCmdWch))
        target = m_wchResult;
    else if (cmd == QLatin1String(mdcproto::kCmdFan))
        target = m_fanResult;
    else if (cmd == QLatin1String(mdcproto::kCmdPowerio))
        target = m_powerioResult;
    else if (cmd == QLatin1String(mdcproto::kCmdLed))
        target = m_ledResult;

    if (target)
        setResult(target, ok, message);
}

void ControlPage::setResult(QLabel *label, bool ok, const QString &message)
{
    label->setText(QStringLiteral("%1 %2")
                       .arg(ok ? QStringLiteral("✓ 下发成功")
                               : QStringLiteral("✗ 下发失败"),
                            message));
    label->setStyleSheet(QStringLiteral("color:%1;")
                             .arg(ok ? Theme::Green.name() : Theme::Red.name()));
}
