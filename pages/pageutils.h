#ifndef PAGEUTILS_H
#define PAGEUTILS_H

#include <QHBoxLayout>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

/**
 * 各页面共用的少量界面小工具。
 * 目的：让页面代码只写"显示什么"，不重复写"怎么排"。
 */
namespace PageUtils
{

    /** 标准的值标签（右对齐、带 rowVal 样式） */
    inline QLabel *valueLabel(const QString &initial = QStringLiteral("--"))
    {
        auto *l = new QLabel(initial);
        l->setObjectName(QStringLiteral("rowVal"));
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return l;
    }

    /** 单色小字数值标签（用于多列并排展示） */
    inline QLabel *plainValue(const QString &initial = QStringLiteral("--"))
    {
        auto *l = new QLabel(initial);
        l->setObjectName(QStringLiteral("rowVal"));
        return l;
    }

    /** 小标题 */
    inline QLabel *sectionLabel(const QString &text)
    {
        auto *l = new QLabel(text);
        l->setObjectName(QStringLiteral("sectionLabel"));
        return l;
    }

    /** 注脚说明 */
    inline QLabel *noteLabel(const QString &text)
    {
        auto *l = new QLabel(text);
        l->setObjectName(QStringLiteral("footer"));
        l->setWordWrap(true);
        return l;
    }

    /** 「键 …… 值」一行 */
    inline QWidget *kvRow(const QString &key, QLabel *value)
    {
        auto *row = new QWidget;
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(8);

        auto *k = new QLabel(key);
        k->setObjectName(QStringLiteral("rowKey"));

        lay->addWidget(k, 0, Qt::AlignVCenter);
        lay->addStretch(1);
        lay->addWidget(value, 0, Qt::AlignVCenter);
        return row;
    }

} // namespace PageUtils

#endif // PAGEUTILS_H
