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

    /**
     * 在标题旁加一个 "?"，鼠标悬停时展开说明（通常用来写"本卡数据来自哪些协议字段"）。
     *
     * 为什么不直接把这些写在界面上：完整字段名对操作员是噪音，只有排查的人才需要，
     * 所以收进悬停说明里。
     */
    QLabel *setHelp(const QString &tooltipText);

private:
    QLabel *m_title = nullptr;
    QLabel *m_badge = nullptr;
    QLabel *m_help = nullptr;
    QVBoxLayout *m_content = nullptr;
    QHBoxLayout *m_header = nullptr;
};

#endif // CARD_H
