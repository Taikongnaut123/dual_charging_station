#ifndef CHARGECURVECHART_H
#define CHARGECURVECHART_H

#include <QDateTime>
#include <QString>
#include <QVector>
#include <QWidget>

/**
 * 实时曲线：双 Y 轴折线图。
 *
 *  - 左轴 / 青色面积线：主序列（默认 充电电流 A）
 *  - 右轴 / 琥珀色线：副序列（默认 电池电压 V）
 *  - X 轴：采样时刻
 *
 * 序列名与单位可配置，所以这张图不绑定具体业务，换个场景也能复用。
 * 纯 QPainter 绘制，不依赖 QtCharts，方便部署到资源受限的工控屏。
 */
class ChargeCurveChart : public QWidget
{
    Q_OBJECT

public:
    /** 一个采样点。y1 走左轴（面积线），y2 走右轴（折线） */
    struct CurveSample
    {
        QDateTime time;
        double y1 = 0.0;
        double y2 = 0.0;
    };

    explicit ChargeCurveChart(QWidget *parent = nullptr);

    /** 设置两条序列的显示名与单位，例如 ("充电电流", "A", "电池电压", "V") */
    void setSeries(const QString &y1Name, const QString &y1Unit,
                   const QString &y2Name, const QString &y2Unit);
    void setSamples(const QVector<CurveSample> &samples);
    void setEmptyText(const QString &text);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<CurveSample> m_samples;
    QString m_emptyText;
    QString m_y1Name;
    QString m_y1Unit;
    QString m_y2Name;
    QString m_y2Unit;
};

#endif // CHARGECURVECHART_H
