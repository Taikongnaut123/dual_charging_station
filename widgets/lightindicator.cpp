#include "lightindicator.h"

#include "theme.h"

#include <QFontMetrics>
#include <QPainter>

LightIndicator::LightIndicator(const QString &caption, QWidget *parent)
    : QWidget(parent), m_caption(caption), m_onColor(Theme::Green), m_offColor(Theme::Track)
{
    setMinimumHeight(22);
}

void LightIndicator::setOn(bool on)
{
    if (m_on == on)
        return;
    m_on = on;
    update();
}

void LightIndicator::setCaption(const QString &caption)
{
    m_caption = caption;
    update();
}

void LightIndicator::setValueText(const QString &text)
{
    if (m_valueText == text)
        return;
    m_valueText = text;
    update();
}

void LightIndicator::setColors(const QColor &onColor, const QColor &offColor)
{
    m_onColor = onColor;
    m_offColor = offColor;
    update();
}

QSize LightIndicator::sizeHint() const
{
    return QSize(160, 22);
}

QSize LightIndicator::minimumSizeHint() const
{
    return QSize(90, 22);
}

void LightIndicator::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor color = m_on ? m_onColor : m_offColor;
    const double cy = height() / 2.0;
    const double cx = 9.0;

    // 亮的时候加一圈光晕，远看也能分辨
    if (m_on)
    {
        QColor glow = color;
        glow.setAlpha(60);
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(QPointF(cx, cy), 7.5, 7.5);
    }

    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QPointF(cx, cy), 4.5, 4.5);

    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);

    const int textLeft = int(cx + 13);
    if (!m_valueText.isEmpty())
    {
        const QFontMetrics fm(f);
        const int w = fm.horizontalAdvance(m_valueText);
        const int right = width() - 2;
        p.setPen(m_on ? Theme::TextPrimary : Theme::TextSecondary);
        p.drawText(QRect(right - w, 0, w, height()),
                   Qt::AlignRight | Qt::AlignVCenter, m_valueText);

        p.setPen(Theme::TextSecondary);
        p.drawText(QRect(textLeft, 0, right - w - textLeft - 6, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, m_caption);
    }
    else
    {
        p.setPen(m_on ? Theme::TextPrimary : Theme::TextSecondary);
        p.drawText(QRect(textLeft, 0, width() - textLeft, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, m_caption);
    }
}
