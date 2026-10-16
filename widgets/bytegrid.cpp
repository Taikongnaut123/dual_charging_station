#include "bytegrid.h"

#include "theme.h"

#include <QPainter>
#include <QtGlobal>

ByteGrid::ByteGrid(QWidget *parent)
    : QWidget(parent)
{
    QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Minimum);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
}

void ByteGrid::setBytes(const QVector<quint8> &bytes)
{
    if (m_bytes == bytes)
        return;
    m_bytes = bytes;
    updateGeometry();
    update();
}

void ByteGrid::setColumns(int columns)
{
    const int c = qMax(1, columns);
    if (m_columns == c)
        return;
    m_columns = c;
    updateGeometry();
    update();
}

void ByteGrid::setCellSize(int px)
{
    const int c = qBound(14, px, 48);
    if (m_cell == c)
        return;
    m_cell = c;
    updateGeometry();
    update();
}

void ByteGrid::setValueAsHex(bool hex)
{
    if (m_hex == hex)
        return;
    m_hex = hex;
    update();
}

void ByteGrid::setEmptyText(const QString &text)
{
    m_emptyText = text;
    update();
}

int ByteGrid::rows() const
{
    if (m_bytes.isEmpty())
        return 1;
    return (m_bytes.size() + m_columns - 1) / m_columns;
}

int ByteGrid::heightForWidth(int w) const
{
    Q_UNUSED(w)
    return rows() * m_cell + (rows() - 1) * gap();
}

QSize ByteGrid::sizeHint() const
{
    return QSize(m_columns * (m_cell + gap()), heightForWidth(width()));
}

QSize ByteGrid::minimumSizeHint() const
{
    return QSize(m_cell * 2, heightForWidth(width()));
}

void ByteGrid::paintEvent(QPaintEvent *)
{
    if (m_bytes.isEmpty())
    {
        QPainter p(this);
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(Theme::TextMuted);
        p.drawText(rect(), Qt::AlignCenter, m_emptyText);
        return;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QFont f = font();
    f.setPixelSize(qMax(9, m_cell / 3));
    f.setBold(true);
    p.setFont(f);

    const int radius = 5;

    for (int i = 0; i < m_bytes.size(); ++i)
    {
        const int row = i / m_columns;
        const int col = i % m_columns;
        const QRectF cell(col * (m_cell + gap()), row * (m_cell + gap()),
                          m_cell, m_cell);

        const int v = m_bytes.at(i);
        // 亮度做底色：0 几乎全暗，255 是强调青
        QColor bg = (v == 0) ? Theme::Track : Theme::Accent;
        bg.setAlpha(v == 0 ? 255 : 40 + 175 * v / 255);

        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(cell, radius, radius);

        p.setPen(v == 0 ? Theme::TextMuted : Theme::TextPrimary);
        const QString text = m_hex
                                 ? QStringLiteral("%1").arg(v, 2, 16, QLatin1Char('0')).toUpper()
                                 : QString::number(v);
        p.drawText(cell, Qt::AlignCenter, text);
    }
}
