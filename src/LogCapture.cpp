#include "LogCapture.h"

#include <QString>
#include <QTime>
#include <QDebug>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define LC_DUP _dup
#define LC_DUP2 _dup2
#define LC_READ(fd, buf, n) _read(fd, buf, static_cast<unsigned int>(n))
#define LC_WRITE(fd, buf, n) _write(fd, buf, static_cast<unsigned int>(n))
#define LC_CLOSE _close
#else
#include <unistd.h>
#define LC_DUP dup
#define LC_DUP2 dup2
#define LC_READ(fd, buf, n) read(fd, buf, n)
#define LC_WRITE(fd, buf, n) write(fd, buf, n)
#define LC_CLOSE close
#endif

namespace {

constexpr size_t kMaxLines = 20000;

std::mutex g_mutex;
std::deque<QString> g_lines;
quint64 g_total = 0; // lines ever captured
bool g_installed = false;

void appendLine(const std::string& raw)
{
    std::string line = raw;
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    const QString text = QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz  "))
        + QString::fromUtf8(line.data(), static_cast<qsizetype>(line.size()));
    std::lock_guard<std::mutex> lock(g_mutex);
    g_lines.push_back(text);
    if (g_lines.size() > kMaxLines) {
        g_lines.pop_front();
    }
    ++g_total;
}

void writeAll(int fd, const char* data, long long size)
{
    while (fd >= 0 && size > 0) {
        const auto n = LC_WRITE(fd, data, static_cast<size_t>(size));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        data += n;
        size -= n;
    }
}

// Reads what the app writes to one of its output streams: passes it on to where it used to
// go (the terminal) and keeps a copy, line by line.
void pump(int readFd, int originalFd)
{
    std::string partial;
    char buffer[4096];
    for (;;) {
        const auto n = LC_READ(readFd, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        writeAll(originalFd, buffer, n);
        partial.append(buffer, static_cast<size_t>(n));
        size_t pos = 0;
        while ((pos = partial.find('\n')) != std::string::npos) {
            appendLine(partial.substr(0, pos));
            partial.erase(0, pos + 1);
        }
        if (partial.size() > 64 * 1024) { // a very long line with no end: show what there is
            appendLine(partial);
            partial.clear();
        }
    }
    if (!partial.empty()) {
        appendLine(partial);
    }
}

bool capture(int fd)
{
    int fds[2] = { -1, -1 };
#ifdef _WIN32
    if (_pipe(fds, 64 * 1024, _O_BINARY) != 0) {
        return false;
    }
#else
    if (pipe(fds) != 0) {
        return false;
    }
#endif
    const int original = LC_DUP(fd); // -1 when there is no terminal (a Windows GUI app)
    if (LC_DUP2(fds[1], fd) < 0) {
        LC_CLOSE(fds[0]);
        LC_CLOSE(fds[1]);
        if (original >= 0) {
            LC_CLOSE(original);
        }
        return false;
    }
    LC_CLOSE(fds[1]);
    // Detached: it lives as long as the app, blocked on the pipe when there's nothing to read.
    std::thread(pump, fds[0], original).detach();
    return true;
}

// Qt's own messages go to stderr as they would by default, so they are captured with the rest.
// (On Windows, Qt would otherwise send them to the debugger instead of a stream.)
void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QByteArray text = qFormatLogMessage(type, context, message).toUtf8() + '\n';
    std::fwrite(text.constData(), 1, static_cast<size_t>(text.size()), stderr);
    std::fflush(stderr);
    if (type == QtFatalMsg) {
        std::abort();
    }
}

} // namespace

namespace LogCapture {

void install()
{
    if (g_installed) {
        return;
    }
    g_installed = true;
#ifdef _WIN32
    // A GUI app may start without standard streams; give them somewhere to write first.
    if (_fileno(stdout) < 0) {
        std::freopen("NUL", "w", stdout);
    }
    if (_fileno(stderr) < 0) {
        std::freopen("NUL", "w", stderr);
    }
#endif
    std::fflush(stdout);
    std::fflush(stderr);
    if (capture(1)) {
        // A pipe would make stdout fully buffered; keep it flushing so lines show up promptly.
#ifdef _WIN32
        std::setvbuf(stdout, nullptr, _IONBF, 0);
#else
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
#endif
    }
    capture(2);
    qInstallMessageHandler(messageHandler);
}

QStringList linesSince(quint64& serial, bool* dropped)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    const quint64 first = g_total - g_lines.size(); // serial of the oldest line still held
    if (dropped) {
        *dropped = serial < first && serial != 0;
    }
    if (serial < first) {
        serial = first;
    }
    QStringList out;
    for (quint64 i = serial - first; i < g_lines.size(); ++i) {
        out << g_lines[static_cast<size_t>(i)];
    }
    serial = g_total;
    return out;
}

}
