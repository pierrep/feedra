#pragma once

#include <QPushButton>
#include <QStyle>

// The flat tab buttons over the sidebar and the bottom panel: the "active" property picks the
// raised look in the theme's style sheet, so it has to be re-polished when it changes.
inline void setTabButtonActive(QPushButton* button, bool active)
{
    if (!button) {
        return;
    }
    button->setProperty("active", active);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
}
