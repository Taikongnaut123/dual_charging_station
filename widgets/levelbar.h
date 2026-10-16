#ifndef LEVELBAR_H
#define LEVELBAR_H

#include <QColor>
#include <QString>
#include <QWidget>

/**
 * 水平量条：左侧固定宽度标题 + 中间进度条 + 右侧数值文本。
 * 用于风扇 PWM、电流占额定值的比例这类 0~100 的量。
 */
class LevelBar : public QWidget
{
    Q_OBJECT

public:
    explicit LevelBar(const QString &caption, QWidget *parent = nullptr);

    void setValue(int value); // 0~100，超出自动裁剪
    void setValueText(const QString &text);
    void setCaption(const QString &caption);
    void setCaptionWidth(int px);
    void setAccent(const QColor &color);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_caption;
    QString m_valueText;
    int m_value = 0;
    int m_captionWidth = 62;
    QColor m_accent;
};

#endif // LEVELBAR_H
