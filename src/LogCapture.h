#pragma once

#include <QStringList>
#include <QtGlobal>

// Keeps a copy of everything the app writes to the terminal (stdout and stderr, which
// includes qDebug/qWarning and the audio libraries' own messages) so it can be shown in
// the Logs window. The terminal still gets the output as before.
namespace LogCapture {

// Call once, first thing in main(), before anything writes output.
void install();

// Lines captured after `serial` (0 for everything still held), each prefixed with the time
// it arrived. Updates `serial` for the next call. `dropped` is set when older lines were
// discarded before they could be read (only the most recent lines are kept).
QStringList linesSince(quint64& serial, bool* dropped = nullptr);

}
