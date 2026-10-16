#ifndef METRICTILE_H
#define METRICTILE_H

#include <QColor>
#include <QFrame>
#include <QString>

class QLabel;

/**
 * 数据小格子：上面一行小标题，中间是数值 + 单位，下面一行可选补充说明。
 */
class MetricTile : public QFrame
{
    Q_OBJECT

public:
    explicit MetricTile(const QString &caption, QWidget *parent = nullptr);

    void setValue(const QString &text);
    void setUnit(const QString &unit);
    void setSubText(const QString &text);
    void setAccent(const QColor &color);

private:
    QLabel *m_caption = nullptr;
    QLabel *m_value = nullptr;
    QLabel *m_unit = nullptr;
    QLabel *m_sub = nullptr;
    QColor m_accent;
};

#endif // METRICTILE_H
