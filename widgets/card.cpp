#include "card.h"

#include "theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVariant>
#include <QVBoxLayout>

Card::Card(const QString &title, QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("card"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 13, 16, 15);
    root->setSpacing(11);

    m_header = new QHBoxLayout;
    m_header->setContentsMargins(0, 0, 0, 0);
    m_header->setSpacing(8);

    m_title = new QLabel(title, this);
    m_title->setObjectName(QStringLiteral("cardTitle"));
    m_header->addWidget(m_title);
    m_header->addStretch(1);
    root->addLayout(m_header);

    m_content = new QVBoxLayout;
    m_content->setContentsMargins(0, 0, 0, 0);
    m_content->setSpacing(9);
    root->addLayout(m_content, 1);
}

void Card::addWidget(QWidget *widget, int stretch)
{
    m_content->addWidget(widget, stretch);
}

void Card::addLayout(QLayout *layout, int stretch)
{
    m_content->addLayout(layout, stretch);
}

QLabel *Card::setBadge(const QString &text, const QString &level)
{
    if (!m_badge)
    {
        m_badge = new QLabel(this);
        m_badge->setObjectName(QStringLiteral("badge"));
        m_badge->setAlignment(Qt::AlignCenter);
        m_header->addWidget(m_badge, 0, Qt::AlignVCenter);
    }
    m_badge->setText(text);
    m_badge->setProperty("level", level);
    Theme::repolish(m_badge);
    return m_badge;
}
