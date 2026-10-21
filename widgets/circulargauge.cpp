#include "circulargauge.h"

#include "theme.h"

#include <QConicalGradient>
#include <QEasingCurve>
#include <QFontMetrics>
#include <QPainter>
#include <QPropertyAnimation>
#include <QTimer>
#include <QtMath>

namespace
{
    const int kStartAngle = 225; // 起始角（Qt 角度：0 在 3 点方向，逆时针为正）
    const int kSpanAngle = -270; // 顺时针扫描 270°
    const int kTickCount = 40;   // 刻度数量
} // namespace

CircularGauge::CircularGauge(QWidget *parent)
    : QWidget(parent), m_accent(Theme::Accent), m_unit(QStringLiteral("%"))
{
    setMinimumSize(Theme::px(100), Theme::px(100));

    m_anim = new QPropertyAnimation(this, QByteArrayLiteral("value"), this);
    m_anim->setDuration(650);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);

    m_flowTimer = new QTimer(this);
    m_flowTimer->setInterval(45);
    connect(m_flowTimer, &QTimer::timeout, this, &CircularGauge::onFlowTick);
}

void CircularGauge::setValue(double v)
{
    v = qBound(0.0, v, 100.0);
    if (qFuzzyCompare(m_value + 1.0, v + 1.0))
        return;
    m_value = v;
    update();
}

void CircularGauge::animateTo(double target)
{
    target = qBound(0.0, target, 100.0);
    if (qFuzzyCompare(m_value + 1.0, target + 1.0))
        return;

    m_anim->stop();
    m_anim->setStartValue(m_value);
    m_anim->setEndValue(target);
    m_anim->start();
}

void CircularGauge::setAccent(const QColor &color)
{
    if (m_accent == color)
        return;
    m_accent = color;
    update();
}

void CircularGauge::setCaption(const QString &text)
{
    m_caption = text;
    update();
}

void CircularGauge::setSubCaption(const QString &text)
{
    m_subCaption = text;
    update();
}

void CircularGauge::setUnit(const QString &unit)
{
    m_unit = unit;
    update();
}

void CircularGauge::setFlowing(bool on)
{
    if (m_flowing == on)
        return;
    m_flowing = on;
    if (on)
        m_flowTimer->start();
    else
    {
        m_flowTimer->stop();
        m_phase = 0.0;
    }
    update();
}

void CircularGauge::onFlowTick()
{
    m_phase += 0.018;
    if (m_phase > 1.0)
        m_phase -= 1.0;
    update();
}

QSize CircularGauge::sizeHint() const
{
    return QSize(230, 230);
}

QSize CircularGauge::minimumSizeHint() const
{
    return QSize(140, 140);
}

void CircularGauge::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const int side = qMin(width(), height());
    const int thickness = qMax(9, int(side * 0.085));
    const QPointF center(width() / 2.0, height() / 2.0);

    const int outer = side - thickness - 8;
    const QRectF arcRect(center.x() - outer / 2.0, center.y() - outer / 2.0, outer, outer);

    // ---------- 刻度 ----------
    const double tickOuter = outer / 2.0 - thickness * 0.85;
    const double tickInner = tickOuter - qMax(4.0, side * 0.028);
    const int filledTicks = int(kTickCount * m_value / 100.0 + 0.5);
    const qreal tickWidth = qMax(qreal(1.5), side * 0.008);
    for (int i = 0; i <= kTickCount; ++i)
    {
        const double deg = kStartAngle + kSpanAngle * i / double(kTickCount);
        const double rad = qDegreesToRadians(deg);
        const QPointF a(center.x() + tickOuter * qCos(rad), center.y() - tickOuter * qSin(rad));
        const QPointF b(center.x() + tickInner * qCos(rad), center.y() - tickInner * qSin(rad));
        QColor c = (i <= filledTicks) ? m_accent : Theme::Track;
        c.setAlpha(i <= filledTicks ? 165 : 255);
        p.setPen(QPen(c, tickWidth, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(a, b);
    }

    // ---------- 轨道 ----------
    QPen trackPen(Theme::Track, thickness, Qt::SolidLine, Qt::RoundCap);
    p.setPen(trackPen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(arcRect, kStartAngle * 16, kSpanAngle * 16);

    // ---------- 进度弧（带辉光） ----------
    if (m_value > 0.5)
    {
        const int span = int(kSpanAngle * 16 * m_value / 100.0);

        QColor glow = m_accent;
        glow.setAlpha(55);
        p.setPen(QPen(glow, thickness + 10, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(arcRect, kStartAngle * 16, span);

        QConicalGradient grad(center, kStartAngle);
        QColor dark = m_accent;
        dark.setAlpha(150);
        grad.setColorAt(0.0, dark);
        grad.setColorAt(qBound(0.0, m_value / 100.0, 1.0), m_accent);
        grad.setColorAt(1.0, m_accent);
        p.setPen(QPen(QBrush(grad), thickness, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(arcRect, kStartAngle * 16, span);
    }

    // ---------- 充电中的流动光点 ----------
    if (m_flowing && m_value > 0.5)
    {
        const double radius = arcRect.width() / 2.0;
        for (int k = 0; k < 3; ++k)
        {
            double t = m_phase - k * 0.09;
            if (t < 0.0)
                t += 1.0;
            if (t > 1.0)
                continue;
            const double deg = kStartAngle + kSpanAngle * t * m_value / 100.0;
            const double rad = qDegreesToRadians(deg);
            const QPointF pt(center.x() + radius * qCos(rad), center.y() - radius * qSin(rad));
            QColor c(255, 255, 255);
            c.setAlphaF(k == 0 ? 0.85 : 0.30);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            const double dot = thickness * (k == 0 ? 0.34 : 0.24);
            p.drawEllipse(pt, dot, dot);
        }
    }

    // ---------- 中心文字 ----------
    QFont valueFont = font();
    valueFont.setPixelSize(qMax(26, int(side * 0.24)));
    valueFont.setBold(true);
    const QFontMetrics vfm(valueFont);
    const QString valueText = QString::number(m_value, 'f', 0);

    QFont unitFont = font();
    unitFont.setPixelSize(qMax(11, int(side * 0.095)));
    unitFont.setBold(true);
    const QFontMetrics ufm(unitFont);
    const int unitW = ufm.horizontalAdvance(m_unit);

    const int valueW = vfm.horizontalAdvance(valueText);
    const int blockW = valueW + 3 + unitW;
    const int baselineTop = int(center.y() - vfm.height() / 2.0);

    p.setFont(valueFont);
    p.setPen(Theme::TextPrimary);
    p.drawText(QRect(int(center.x() - blockW / 2.0), baselineTop, valueW, vfm.height()),
               Qt::AlignLeft | Qt::AlignVCenter, valueText);

    p.setFont(unitFont);
    QColor unitColor = m_accent;
    unitColor.setAlpha(220);
    p.setPen(unitColor);
    p.drawText(QRect(int(center.x() - blockW / 2.0) + valueW + 3, baselineTop + vfm.height() / 2,
                     unitW + 4, vfm.height()),
               Qt::AlignLeft | Qt::AlignVCenter, m_unit);

    if (!m_caption.isEmpty())
    {
        QFont cf = font();
        cf.setPixelSize(qMax(11, int(side * 0.072)));
        p.setFont(cf);
        p.setPen(Theme::TextSecondary);
        p.drawText(QRect(0, int(center.y() - side * 0.36), width(), int(side * 0.16)),
                   Qt::AlignHCenter | Qt::AlignVCenter, m_caption);
    }

    if (!m_subCaption.isEmpty())
    {
        QFont sf = font();
        sf.setPixelSize(qMax(10, int(side * 0.065)));
        p.setFont(sf);
        p.setPen(Theme::TextMuted);
        p.drawText(QRect(0, int(center.y() + side * 0.22), width(), int(side * 0.16)),
                   Qt::AlignHCenter | Qt::AlignVCenter, m_subCaption);
    }
}
