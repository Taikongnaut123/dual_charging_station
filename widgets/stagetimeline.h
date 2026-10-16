#ifndef STAGETIMELINE_H
#define STAGETIMELINE_H

#include <QColor>
#include <QStringList>
#include <QWidget>

/**
 * 通用阶段时间轴：一排节点 + 连线 + 文字。
 *
 * 由调用方决定有几步、每步叫什么，所以既能画充电流程，也能复用到别处。
 * 节点状态：
 *   index <  reached  已走过（实心）
 *   index == current  当前（实心 + 光晕）
 *   其它              未到达（暗色）
 * current == -1 表示尚未开始；faulted 为真时整条进度转红并保留已到达的节点。
 */
class StageTimeline : public QWidget
{
    Q_OBJECT

public:
    explicit StageTimeline(QWidget *parent = nullptr);

    void setLabels(const QStringList &labels);
    void setProgress(int current, int reached, bool faulted);
    void setCurrent(int current);
    void setAccent(const QColor &color);
    void reset();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QStringList m_labels;
    int m_current = -1; // 当前步骤，-1 = 尚未开始，-2 = 故障
    int m_reached = -1; // 已到达的最远步骤
    bool m_faulted = false;
    QColor m_accent;
};

#endif // STAGETIMELINE_H
