#include "lightindicator.h"

#include "theme.h"

#include <QFontMetrics>
#include <QPainter>

LightIndicator::LightIndicator(const QString &caption, QWidget *parent)
    : QWidget(parent), m_caption(caption), m_onColor(Theme::Green), m_offColor(Theme::Track)
{
    setMinimumHeight(Theme::px(22));
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
    return QSize(Theme::px(160), Theme::px(22));
}

QSize LightIndicator::minimumSizeHint() const
{
    return QSize(Theme::px(90), Theme::px(22));
}

void LightIndicator::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor color = m_on ? m_onColor : m_offColor;
    const double cy = height() / 2.0;

    // 自绘控件不吃 QSS：字号的几何都必须自己走 Theme 的缩放，
    // 否则字号变了它还是一成不变的小圆点小字。
    const double dot = Theme::px(4.5);
    const double cx = Theme::px(9.0);

    // 亮的时候加一圈光晕，远看也能分辨
    if (m_on)
    {
        QColor glow = color;
        glow.setAlpha(60);
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(QPointF(cx, cy), dot * 1.67, dot * 1.67);
    }

    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QPointF(cx, cy), dot, dot);

    QFont f = font();
    f.setPixelSize(Theme::fontPx(13));
    p.setFont(f);

    const int textLeft = int(cx + Theme::px(13));
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
