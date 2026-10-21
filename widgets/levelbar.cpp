#include "levelbar.h"

#include "theme.h"

#include <QFontMetrics>
#include <QPainter>
#include <QtGlobal>

LevelBar::LevelBar(const QString &caption, QWidget *parent)
    : QWidget(parent), m_caption(caption), m_accent(Theme::Accent)
{
    setMinimumHeight(Theme::px(22));
}

void LevelBar::setValue(int value)
{
    const int v = qBound(0, value, 100);
    if (m_value == v)
        return;
    m_value = v;
    update();
}

void LevelBar::setValueText(const QString &text)
{
    if (m_valueText == text)
        return;
    m_valueText = text;
    update();
}

void LevelBar::setCaption(const QString &caption)
{
    m_caption = caption;
    update();
}

void LevelBar::setCaptionWidth(int px)
{
    m_captionWidth = px;
    update();
}

void LevelBar::setAccent(const QColor &color)
{
    if (m_accent == color)
        return;
    m_accent = color;
    update();
}

QSize LevelBar::sizeHint() const
{
    return QSize(200, 22);
}

QSize LevelBar::minimumSizeHint() const
{
    return QSize(120, 22);
}

void LevelBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);
    const QFontMetrics fm(f);

    // ---- 左侧标题 ----
    p.setPen(Theme::TextSecondary);
    p.drawText(QRect(0, 0, m_captionWidth, height()),
               Qt::AlignLeft | Qt::AlignVCenter, m_caption);

    // ---- 右侧数值 ----
    int valueWidth = 0;
    if (!m_valueText.isEmpty())
    {
        valueWidth = fm.horizontalAdvance(m_valueText) + 4;
        p.setPen(Theme::TextPrimary);
        p.drawText(QRect(width() - valueWidth, 0, valueWidth, height()),
                   Qt::AlignRight | Qt::AlignVCenter, m_valueText);
    }

    // ---- 中间条 ----
    const int barLeft = m_captionWidth + 6;
    const int barRight = width() - valueWidth - 8;
    const int barWidth = barRight - barLeft;
    if (barWidth < 8)
        return;

    const int h = 7;
    const double y = (height() - h) / 2.0;
    const QRectF track(barLeft, y, barWidth, h);

    p.setPen(Qt::NoPen);
    p.setBrush(Theme::Track);
    p.drawRoundedRect(track, h / 2.0, h / 2.0);

    if (m_value > 0)
    {
        QRectF fill = track;
        fill.setWidth(barWidth * m_value / 100.0);
        p.setBrush(m_accent);
        p.drawRoundedRect(fill, h / 2.0, h / 2.0);
    }
}
