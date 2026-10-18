#pragma once

// Building blocks of the Settings and Theme pages: a page header with "Back to scenes", a
// scrolling column of cards, and cards of label/field rows.

#include <QString>

class QFrame;
class QGridLayout;
class QPushButton;
class QVBoxLayout;
class QWidget;

// A rounded "card" grouping related settings: title, optional one-line hint, then
// label/field rows in a two-column grid.
struct SettingsCard {
    QFrame* frame = nullptr;
    QGridLayout* grid = nullptr;
    int row = 0;

    void addRow(const QString& label, QWidget* field, const QString& hint = QString());
    void addFullRow(QWidget* widget);
};

SettingsCard makeCard(QWidget* parent, const QString& title, const QString& hint = QString());

// A scrolling column of cards, kept to a readable width and left-aligned, added to `pageLayout`.
QVBoxLayout* makeCardColumn(QWidget* page, QVBoxLayout* pageLayout);

// Title, subtitle and a "Back to scenes" button (returned through `back`).
QWidget* makePageHeader(QWidget* page, const QString& title, const QString& subtitle, QPushButton** back);
