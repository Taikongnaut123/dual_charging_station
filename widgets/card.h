#ifndef CARD_H
#define CARD_H

#include <QFrame>
#include <QString>

class QLabel;
class QVBoxLayout;
class QHBoxLayout;
class QLayout;

/**
 * 统一风格的卡片容器：左上角标题 + 右上角可选徽标 + 内容区。
 */
class Card : public QFrame
{
    Q_OBJECT

public:
    explicit Card(const QString &title, QWidget *parent = nullptr);

    QVBoxLayout *contentLayout() const { return m_content; }
    void addWidget(QWidget *widget, int stretch = 0);
    void addLayout(QLayout *layout, int stretch = 0);

    /** level 取值：dim / info / ok / warn / err；返回徽标控件便于后续刷新 */
    QLabel *setBadge(const QString &text, const QString &level = QStringLiteral("dim"));

private:
    QLabel *m_title = nullptr;
    QLabel *m_badge = nullptr;
    QVBoxLayout *m_content = nullptr;
    QHBoxLayout *m_header = nullptr;
};

#endif // CARD_H
