#pragma once

#include <QDebug>
#include <QString>
#include <filesystem>

inline QDebug operator<<(QDebug debug, const std::filesystem::path& path)
{
    QDebugStateSaver saver(debug);
    debug.noquote() << QString::fromStdString(path.string());
    return debug;
}
