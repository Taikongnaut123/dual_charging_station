#ifndef LIGHTINDICATOR_H
#define LIGHTINDICATOR_H

#include <QColor>
#include <QString>
#include <QWidget>

/**
 * 指示灯行：彩色圆点 + 名称 + 右侧状态文本。
 * 用于红外管、继电器、拨码开关、电源输出通道这类"开/关"量的显示。
 */
class LightIndicator : public QWidget
{
    Q_OBJECT

public:
    explicit LightIndicator(const QString &caption, QWidget *parent = nullptr);

    void setOn(bool on);
    bool isOn() const { return m_on; }
    void setCaption(const QString &caption);
    void setValueText(const QString &text);
    /** 自定义亮/灭颜色；默认亮=绿、灭=暗蓝 */
    void setColors(const QColor &onColor, const QColor &offColor);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_caption;
    QString m_valueText;
    bool m_on = false;
    QColor m_onColor;
    QColor m_offColor;
};

#endif // LIGHTINDICATOR_H
