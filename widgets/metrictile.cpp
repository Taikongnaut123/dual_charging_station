#include "metrictile.h"

#include "theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

MetricTile::MetricTile(const QString &caption, QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("tile"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(Theme::px(12), Theme::px(8), Theme::px(12), Theme::px(9));
    root->setSpacing(Theme::px(2));

    m_caption = new QLabel(caption, this);
    m_caption->setObjectName(QStringLiteral("tileCaption"));
    root->addWidget(m_caption);

    auto *valueRow = new QHBoxLayout;
    valueRow->setContentsMargins(0, 0, 0, 0);
    valueRow->setSpacing(Theme::px(4));

    m_value = new QLabel(QStringLiteral("--"), this);
    m_value->setObjectName(QStringLiteral("tileValue"));
    m_unit = new QLabel(this);
    m_unit->setObjectName(QStringLiteral("tileUnit"));

    valueRow->addWidget(m_value);
    valueRow->addWidget(m_unit, 0, Qt::AlignBottom);
    valueRow->addStretch(1);
    root->addLayout(valueRow);

    m_sub = new QLabel(this);
    m_sub->setObjectName(QStringLiteral("tileSub"));
    m_sub->setVisible(false);
    root->addWidget(m_sub);
}

void MetricTile::setValue(const QString &text)
{
    m_value->setText(text);
}

void MetricTile::setUnit(const QString &unit)
{
    m_unit->setText(unit);
}

void MetricTile::setSubText(const QString &text)
{
    m_sub->setText(text);
    m_sub->setVisible(!text.isEmpty());
}

void MetricTile::setAccent(const QColor &color)
{
    if (m_accent == color)
        return;
    m_accent = color;
    m_value->setStyleSheet(QStringLiteral("color:%1;").arg(color.name()));
}
