#ifndef BYTEGRID_H
#define BYTEGRID_H

#include <QString>
#include <QVector>
#include <QWidget>

/**
 * 字节阵列显示：把 uint8[] 画成一片小方格（十六进制）。
 * 用于 LED 三通道原始数据 (uint8[8]) 与 RGB 数据 (uint8[16])。
 * 格子底色深浅随数值变化，一眼能看出哪几路是亮的。
 */
class ByteGrid : public QWidget
{
    Q_OBJECT

public:
    explicit ByteGrid(QWidget *parent = nullptr);

    void setBytes(const QVector<quint8> &bytes);
    void setColumns(int columns); // 每行几个格子
    void setCellSize(int px);     // 格子边长
    void setValueAsHex(bool hex); // 十六进制 / 十进制
    void setEmptyText(const QString &text);

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int gap() const { return 4; }
    int rows() const;

    QVector<quint8> m_bytes;
    int m_columns = 8;
    int m_cell = 26;
    bool m_hex = true;
    QString m_emptyText;
};

#endif // BYTEGRID_H
