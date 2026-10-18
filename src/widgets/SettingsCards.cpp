#include "SettingsCards.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

void SettingsCard::addRow(const QString& label, QWidget* field, const QString& hint)
{
    auto* caption = new QLabel(label, frame);
    grid->addWidget(caption, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(field, row, 1);
    ++row;
    if (!hint.isEmpty()) {
        auto* note = new QLabel(hint, frame);
        note->setObjectName(QStringLiteral("FieldHint"));
        note->setWordWrap(true);
        grid->addWidget(note, row, 1);
        ++row;
    }
}

void SettingsCard::addFullRow(QWidget* widget)
{
    grid->addWidget(widget, row, 0, 1, 2);
    ++row;
}

SettingsCard makeCard(QWidget* parent, const QString& title, const QString& hint)
{
    SettingsCard card;
    card.frame = new QFrame(parent);
    card.frame->setObjectName(QStringLiteral("SettingsCard"));
    card.frame->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(card.frame);
    layout->setContentsMargins(20, 18, 20, 20);
    layout->setSpacing(4);
    auto* heading = new QLabel(title, card.frame);
    heading->setObjectName(QStringLiteral("CardTitle"));
    layout->addWidget(heading);
    if (!hint.isEmpty()) {
        auto* note = new QLabel(hint, card.frame);
        note->setObjectName(QStringLiteral("CardHint"));
        note->setWordWrap(true);
        layout->addWidget(note);
    }
    layout->addSpacing(12);
    card.grid = new QGridLayout();
    card.grid->setContentsMargins(0, 0, 0, 0);
    card.grid->setHorizontalSpacing(16);
    card.grid->setVerticalSpacing(12);
    card.grid->setColumnMinimumWidth(0, 150);
    card.grid->setColumnStretch(1, 1);
    layout->addLayout(card.grid);
    return card;
}

// A scrolling column of cards, kept to a readable width and left-aligned.
QVBoxLayout* makeCardColumn(QWidget* page, QVBoxLayout* pageLayout)
{
    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* wrapper = new QWidget(scroll);
    wrapper->setObjectName(QStringLiteral("SettingsPage"));
    wrapper->setAttribute(Qt::WA_StyledBackground, true);
    auto* row = new QHBoxLayout(wrapper);
    row->setContentsMargins(0, 0, 12, 0);
    auto* column = new QWidget(wrapper);
    column->setObjectName(QStringLiteral("SettingsPage"));
    column->setMaximumWidth(760);
    auto* cards = new QVBoxLayout(column);
    cards->setContentsMargins(0, 4, 0, 16);
    cards->setSpacing(16);
    row->addWidget(column, 1);
    row->addStretch(0);
    scroll->setWidget(wrapper);
    pageLayout->addWidget(scroll, 1);
    return cards;
}

QWidget* makePageHeader(QWidget* page, const QString& title, const QString& subtitle, QPushButton** backOut)
{
    auto* header = new QWidget(page);
    auto* row = new QHBoxLayout(header);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(16);
    auto* text = new QVBoxLayout();
    text->setSpacing(2);
    auto* heading = new QLabel(title, header);
    heading->setObjectName(QStringLiteral("SettingsTitle"));
    text->addWidget(heading);
    auto* sub = new QLabel(subtitle, header);
    sub->setObjectName(QStringLiteral("PageSubtitle"));
    text->addWidget(sub);
    row->addLayout(text, 1);
    auto* back = new QPushButton(QObject::tr("Back to scenes"), header);
    back->setCursor(Qt::PointingHandCursor);
    back->setToolTip(QObject::tr("Return to the pad grid (Ctrl+1)"));
    if (backOut) {
        *backOut = back;
    }
    row->addWidget(back, 0, Qt::AlignTop);
    return header;
}

