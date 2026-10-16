#include "chargecurvechart.h"

#include "theme.h"

#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <cmath>

namespace
{

    /*
     * 把坐标轴最大值规整为：rows 等分、且每一格的数值都是 1/2/2.5/5 × 10^n。
     * 这样刻度标签会像 0 / 10 / 20 / 30 / 40 而不是 0 / 12.5 / 25 / 38 / 50。
     */
    double niceAxisMax(double maxValue, double minimum, int rows, double *stepOut)
    {
        const double target = qMax(maxValue, minimum);
        const double rawStep = target / rows;
        const double e = std::pow(10.0, std::floor(std::log10(rawStep)));
        const double m = rawStep / e;
        double n = 10.0;
        if (m <= 1.0)
            n = 1.0;
        else if (m <= 2.0)
            n = 2.0;
        else if (m <= 2.5)
            n = 2.5;
        else if (m <= 5.0)
            n = 5.0;
        const double step = n * e;
        if (stepOut)
            *stepOut = step;
        return step * rows;
    }

    int decimalsFor(double step)
    {
        if (step >= 1.0)
            return 0;
        if (step >= 0.1)
            return 1;
        return 2;
    }

    QString axisLabel(double v, int decimals)
    {
        return QString::number(v, 'f', decimals);
    }

} // namespace

ChargeCurveChart::ChargeCurveChart(QWidget *parent)
    : QWidget(parent), m_emptyText(QStringLiteral("等待充电数据…")),
      m_y1Name(QStringLiteral("充电电流")), m_y1Unit(QStringLiteral("A")),
      m_y2Name(QStringLiteral("电池电压")), m_y2Unit(QStringLiteral("V"))
{
    setMinimumHeight(150);
}

QSize ChargeCurveChart::sizeHint() const
{
    return QSize(360, 220);
}

QSize ChargeCurveChart::minimumSizeHint() const
{
    return QSize(240, 140);
}

void ChargeCurveChart::setSeries(const QString &y1Name, const QString &y1Unit,
                                 const QString &y2Name, const QString &y2Unit)
{
    m_y1Name = y1Name;
    m_y1Unit = y1Unit;
    m_y2Name = y2Name;
    m_y2Unit = y2Unit;
    update();
}

void ChargeCurveChart::setSamples(const QVector<CurveSample> &samples)
{
    m_samples = samples;
    update();
}

void ChargeCurveChart::setEmptyText(const QString &text)
{
    m_emptyText = text;
    update();
}

void ChargeCurveChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    QFont labelFont = font();
    labelFont.setPixelSize(10);
    const QFontMetrics labelFm(labelFont);

    const int legendH = 20;
    const int axisW = qMax(30, labelFm.horizontalAdvance(QStringLiteral("0.0")) + 8);
    const int bottomH = qMax(15, labelFm.height() + 3);

    QRectF plot(axisW, legendH + 4, width() - axisW * 2, height() - legendH - 4 - bottomH);
    if (plot.width() < 50 || plot.height() < 40)
        return;

    // ---------- 面板底 ----------
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(13, 23, 41, 190));
    p.drawRoundedRect(plot.adjusted(-1, -1, 1, 1), 8, 8);

    // ---------- 图例 ----------
    {
        QFont legendFont = font();
        legendFont.setPixelSize(11);
        p.setFont(legendFont);

        const int y = legendH / 2 + 2;
        int x = int(plot.left()) + 2;

        p.setPen(Qt::NoPen);
        p.setBrush(Theme::Accent);
        p.drawEllipse(QPointF(x + 3, y), 3.5, 3.5);
        p.setPen(Theme::TextSecondary);
        x += 12;
        const QString t1 = m_y1Name + QLatin1Char(' ') + m_y1Unit;
        p.drawText(QRect(x, y - 9, 96, 18), Qt::AlignLeft | Qt::AlignVCenter, t1);
        x += labelFm.horizontalAdvance(t1) + 22;

        p.setPen(Qt::NoPen);
        p.setBrush(Theme::Amber);
        p.drawEllipse(QPointF(x + 3, y), 3.5, 3.5);
        p.setPen(Theme::TextSecondary);
        x += 12;
        p.drawText(QRect(x, y - 9, 96, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   m_y2Name + QLatin1Char(' ') + m_y2Unit);

        QFont hintFont = font();
        hintFont.setPixelSize(10);
        p.setFont(hintFont);
        p.setPen(Theme::TextMuted);
        p.drawText(QRect(int(plot.right()) - 90, y - 9, 90, 18),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("近 %1 个采样点").arg(m_samples.size()));
    }

    if (m_samples.isEmpty())
    {
        p.setFont(font());
        p.setPen(Theme::TextMuted);
        p.drawText(plot, Qt::AlignCenter, m_emptyText);
        return;
    }

    // ---------- 量程 ----------
    const int rows = 4;
    const int cols = 4;

    double maxY1 = 0.0;
    double maxY2 = 0.0;
    for (const CurveSample &s : qAsConst(m_samples))
    {
        maxY1 = qMax(maxY1, s.y1);
        maxY2 = qMax(maxY2, s.y2);
    }
    double stepY1 = 0.0;
    double stepY2 = 0.0;
    const double rangeY1 = niceAxisMax(maxY1 * 1.15, 5.0, rows, &stepY1);
    const double rangeY2 = niceAxisMax(maxY2 * 1.15, 12.0, rows, &stepY2);
    const int decimalsY1 = decimalsFor(stepY1);
    const int decimalsY2 = decimalsFor(stepY2);

    // ---------- 网格与坐标轴 ----------
    QPen gridPen(QColor(30, 44, 70, 170), 1, Qt::DotLine);
    QPen framePen(QColor(38, 56, 88, 220), 1);

    p.setFont(labelFont);
    for (int i = 0; i <= rows; ++i)
    {
        const double y = plot.top() + plot.height() * i / rows;
        p.setPen(i == rows ? framePen : gridPen);
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));

        const double vY1 = rangeY1 * (rows - i) / rows;
        const double vY2 = rangeY2 * (rows - i) / rows;

        p.setPen(Theme::TextMuted);
        p.drawText(QRectF(plot.left() - axisW, y - 8, axisW - 6, 16),
                   Qt::AlignRight | Qt::AlignVCenter, axisLabel(vY1, decimalsY1));
        p.drawText(QRectF(plot.right() + 6, y - 8, axisW - 6, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, axisLabel(vY2, decimalsY2));
    }
    for (int i = 0; i <= cols; ++i)
    {
        const double x = plot.left() + plot.width() * i / cols;
        p.setPen(i == 0 || i == cols ? framePen : gridPen);
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
    p.setPen(Theme::TextMuted);
    p.drawText(QRectF(plot.left() - axisW, plot.top() - 13, axisW - 6, 12),
               Qt::AlignRight | Qt::AlignVCenter, m_y1Unit);
    p.drawText(QRectF(plot.right() + 6, plot.top() - 13, axisW - 6, 12),
               Qt::AlignLeft | Qt::AlignVCenter, m_y2Unit);

    // ---------- 折线数据 ----------
    const int n = m_samples.size();
    QPolygonF poly1;
    QPolygonF poly2;
    poly1.reserve(n);
    poly2.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        const double x = (n == 1) ? plot.center().x()
                                  : plot.left() + plot.width() * i / double(n - 1);
        poly1 << QPointF(x, plot.bottom() - plot.height() * m_samples[i].y1 / rangeY1);
        poly2 << QPointF(x, plot.bottom() - plot.height() * m_samples[i].y2 / rangeY2);
    }

    // 主序列面积
    if (n >= 2)
    {
        QPainterPath area;
        area.moveTo(poly1.first().x(), plot.bottom());
        for (const QPointF &pt : qAsConst(poly1))
            area.lineTo(pt);
        area.lineTo(poly1.last().x(), plot.bottom());
        area.closeSubpath();

        QLinearGradient g(0, plot.top(), 0, plot.bottom());
        QColor c1 = Theme::Accent;
        c1.setAlpha(105);
        QColor c2 = Theme::Accent;
        c2.setAlpha(6);
        g.setColorAt(0.0, c1);
        g.setColorAt(1.0, c2);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawPath(area);
    }

    // 曲线
    p.setBrush(Qt::NoBrush);
    if (n >= 2)
    {
        p.setPen(QPen(Theme::Accent, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(poly1);
        p.setPen(QPen(Theme::Amber, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolyline(poly2);
    }

    // 最新点标记
    const QPointF lastP = poly1.last();
    const QPointF lastI = poly2.last();
    p.setPen(Qt::NoPen);
    QColor pFill = Theme::Accent;
    pFill.setAlpha(70);
    p.setBrush(pFill);
    p.drawEllipse(lastP, 6, 6);
    p.setBrush(Theme::Accent);
    p.drawEllipse(lastP, 3.2, 3.2);
    p.setBrush(Theme::Amber);
    p.drawEllipse(lastI, 3.0, 3.0);

    // ---------- X 轴时间 ----------
    p.setFont(labelFont);
    p.setPen(Theme::TextMuted);
    const double labelY = plot.bottom() + 3;
    const int timeW = labelFm.horizontalAdvance(QStringLiteral("00:00:00")) + 8;
    const int indices[3] = {0, n / 2, n - 1};
    for (int k = 0; k < 3; ++k)
    {
        const int idx = qBound(0, indices[k], n - 1);
        if (k == 1 && n < 3)
            continue;

        double boxLeft = plot.left();
        Qt::Alignment align = Qt::AlignLeft | Qt::AlignTop;
        if (k == 1)
        {
            boxLeft = plot.center().x() - timeW / 2.0;
            align = Qt::AlignHCenter | Qt::AlignTop;
        }
        else if (k == 2)
        {
            boxLeft = plot.right() - timeW;
            align = Qt::AlignRight | Qt::AlignTop;
        }

        const QString text = m_samples[idx].time.toString(QStringLiteral("HH:mm:ss"));
        p.drawText(QRectF(boxLeft, labelY, timeW, bottomH), align, text);
    }
}
