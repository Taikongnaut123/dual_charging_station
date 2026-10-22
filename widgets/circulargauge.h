#ifndef CIRCULARGAUGE_H
#define CIRCULARGAUGE_H

#include <QColor>
#include <QWidget>

class QPropertyAnimation;
class QTimer;

/**
 * 环形电量表（SOC）。
 *
 * - 270° 环形刻度 + 进度弧；
 * - value 变化带动画平滑过渡；
 * - 充电中外环有流动光点，直观表达"正在充电"。
 */
class CircularGauge : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(double value READ value WRITE setValue)

public:
    explicit CircularGauge(QWidget *parent = nullptr);

    double value() const { return m_value; }
    void setValue(double v);       // 直接赋值（属性动画也会调用它）
    void animateTo(double target); // 平滑过渡到目标值

    void setAccent(const QColor &color);
    QColor accent() const { return m_accent; }

    void setCaption(const QString &text);    // 数值上方小字
    void setSubCaption(const QString &text); // 数值下方小字
    void setUnit(const QString &unit);       // 单位，默认 "%"
    void setFlowing(bool on);                // 是否显示流动光点

    /**
     * 置灰：本口未启用（link_status = 0）时用。
     * 自绘控件不受 QSS 影响，所以"变灰"必须在绘制里做。
     */
    void setDimmed(bool on);
    bool isDimmed() const { return m_dimmed; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void onFlowTick();

    double m_value = 0.0;
    QColor m_accent;
    QString m_caption;
    QString m_subCaption;
    QString m_unit;

    QPropertyAnimation *m_anim = nullptr;
    QTimer *m_flowTimer = nullptr;
    double m_phase = 0.0;
    bool m_flowing = false;
    bool m_dimmed = false;
};

#endif // CIRCULARGAUGE_H
