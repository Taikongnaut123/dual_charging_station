#include "stagetimeline.h"

#include "theme.h"

#include <QFontMetrics>
#include <QPainter>
#include <QStringList>

namespace
{
    const double kSidePad = 34.0;
    const double kDotY = 13.0;
} // namespace

StageTimeline::StageTimeline(QWidget *parent)
    : QWidget(parent), m_accent(Theme::Accent)
{
    m_labels = QStringList{QStringLiteral("步骤1"), QStringLiteral("步骤2"),
                           QStringLiteral("步骤3")};
    setMinimumHeight(46);
}

void StageTimeline::setLabels(const QStringList &labels)
{
    if (m_labels == labels)
        return;
    m_labels = labels;
    update();
}

void StageTimeline::setProgress(int current, int reached, bool faulted)
{
    if (m_current == current && m_reached == reached && m_faulted == faulted)
        return;
    m_current = current;
    m_reached = reached;
    m_faulted = faulted;
    update();
}

void StageTimeline::setCurrent(int current)
{
    setProgress(current, qMax(m_reached, current), m_faulted);
}

void StageTimeline::setAccent(const QColor &color)
{
    if (m_accent == color)
        return;
    m_accent = color;
    update();
}

void StageTimeline::reset()
{
    setProgress(-1, -1, false);
}

QSize StageTimeline::sizeHint() const
{
    return QSize(380, 52);
}

QSize StageTimeline::minimumSizeHint() const
{
    return QSize(200, 46);
}

void StageTimeline::paintEvent(QPaintEvent *)
{
    const int n = m_labels.size();
    if (n < 2 || width() < 120)
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QFont labelFont = font();
    labelFont.setPixelSize(11);

    const double span = width() - kSidePad * 2.0;
    QVector<double> xs;
    xs.reserve(n);
    for (int i = 0; i < n; ++i)
        xs << kSidePad + span * i / double(n - 1);

    const QColor active = m_faulted ? Theme::Red : m_accent;

    // ---- 连接线 ----
    p.setPen(QPen(Theme::Track, 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(xs.first(), kDotY), QPointF(xs.last(), kDotY));

    if (m_reached >= 1)
    {
        p.setPen(QPen(active, 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(xs.first(), kDotY), QPointF(xs[qMin(m_reached, n - 1)], kDotY));
    }

    // ---- 节点 + 文字 ----
    p.setFont(labelFont);
    for (int i = 0; i < n; ++i)
    {
        const bool done = (i <= m_reached);
        const bool current = (i == m_current);

        if (current)
        {
            QColor glow = active;
            glow.setAlpha(60);
            p.setPen(Qt::NoPen);
            p.setBrush(glow);
            p.drawEllipse(QPointF(xs[i], kDotY), 9.0, 9.0);
        }

        p.setPen(Qt::NoPen);
        p.setBrush(done ? active : Theme::Track);
        p.drawEllipse(QPointF(xs[i], kDotY), current ? 5.5 : 4.5, current ? 5.5 : 4.5);

        if (current)
        {
            p.setBrush(QColor(255, 255, 255, 220));
            p.drawEllipse(QPointF(xs[i], kDotY), 2.0, 2.0);
        }

        QColor textColor = Theme::TextMuted;
        if (done)
            textColor = Theme::TextSecondary;
        if (current)
            textColor = active;

        p.setPen(textColor);
        const QRectF box(xs[i] - 46.0, kDotY + 9.0, 92.0, 17.0);
        p.drawText(box, Qt::AlignHCenter | Qt::AlignVCenter, m_labels.at(i));
    }
}
