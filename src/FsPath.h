#pragma once

#include <QString>

#include <filesystem>

// QString <-> std::filesystem::path without losing characters.
//
// On Windows a path built from a std::string is read in the ANSI code page, so a UTF-8
// string from QString::toStdString() turns "é" or any CJK name into the wrong file. Build
// paths from UTF-16 there. Elsewhere paths are bytes, and UTF-8 is what the system uses.
namespace FsPath {

inline std::filesystem::path fromQString(const QString& path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

inline QString toQString(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

} // namespace FsPath
