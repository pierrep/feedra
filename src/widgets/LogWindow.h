#pragma once

#include <QWidget>

class QCheckBox;
class QPlainTextEdit;
class QTimer;

// A window showing everything Feedra writes to the terminal, newest at the bottom.
class LogWindow : public QWidget
{
    Q_OBJECT
public:
    explicit LogWindow(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void pull();

    QPlainTextEdit* m_text = nullptr;
    QCheckBox* m_follow = nullptr;
    QTimer* m_timer = nullptr;
    quint64 m_serial = 0;
};
