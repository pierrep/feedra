#include "SampleLoadQueue.h"

#include "PeakStore.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <deque>
#include <mutex>
#include <utility>

struct SampleLoadQueue::State {
    std::atomic<bool> shuttingDown{false};
    std::mutex mutex;
    std::deque<SampleLoadResult> uploads;
};

SampleLoadQueue::SampleLoadQueue(QObject* parent)
    : QObject(parent)
    , m_state(std::make_shared<State>())
    , m_pool(new QThreadPool(this))
{
    const int threads = std::max(1, std::min(4, QThread::idealThreadCount()));
    m_pool->setMaxThreadCount(threads);
}

SampleLoadQueue::~SampleLoadQueue()
{
    shutdown();
}

void SampleLoadQueue::enqueue(const SampleLoadJob& job)
{
    if (m_settled >= m_total) {
        m_settled = 0;
        m_total = 0;
        m_activeFile.clear();
    }
    ++m_total;

    const std::shared_ptr<State> state = m_state;
    // Taken here, on the UI thread, so the store is never first created on a worker.
    PeakStore* peaks = &PeakStore::instance();
    m_pool->start([state, job, peaks]() {
        SampleLoadResult result;
        result.job = job;
        const bool stale = !job.generationToken || job.generationToken->load() != job.generation;
        if (!state->shuttingDown.load() && !stale) {
            result.audio = OpenALSoundPlayer::decodeFile(std::filesystem::path(job.path.toStdString()), job.stream);
            // Bring the saved waveform in with the sample, so the pad strip and the Waveform
            // tab show it straight away (no scan if the cache file is valid).
            peaks->preload(job.path);
        }
        if (state->shuttingDown.load()) {
            return;
        }
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->shuttingDown.load()) {
            return;
        }
        state->uploads.push_back(std::move(result));
    });
}

void SampleLoadQueue::drain(int budgetMs, const std::function<void(SampleLoadResult)>& commit)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < budgetMs) {
        SampleLoadResult result;
        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            if (m_state->uploads.empty()) {
                break;
            }
            result = std::move(m_state->uploads.front());
            m_state->uploads.pop_front();
        }
        m_activeFile = QFileInfo(result.job.path).fileName();
        if (commit) {
            commit(std::move(result));
        }
        ++m_settled;
    }
    if (m_settled >= m_total) {
        m_activeFile.clear();
    }
}

void SampleLoadQueue::shutdown()
{
    if (!m_state) {
        return;
    }
    m_state->shuttingDown.store(true);
    if (m_pool) {
        m_pool->clear();
        m_pool->waitForDone();
    }
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->uploads.clear();
    }
    m_settled = m_total;
    m_activeFile.clear();
}
