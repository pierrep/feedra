#pragma once

#include "OpenALSoundPlayer.h"

#include <QObject>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>

struct SampleLoadJob {
    int sceneId = 0;
    int padId = 0;
    int sampleIndex = 0;
    int generation = 0;
    std::shared_ptr<std::atomic<int>> generationToken;
    QString path;
    bool stream = true;
    // Set when re-decoding an already loaded sample (e.g. toggling spatialised stereo).
    int reloadSampleId = -1;
    int reloadSerial = 0;
};

struct SampleLoadResult {
    SampleLoadJob job;
    DecodedAudio audio;
};

class SampleLoadQueue : public QObject
{
public:
    explicit SampleLoadQueue(QObject* parent = nullptr);
    ~SampleLoadQueue() override;

    void enqueue(const SampleLoadJob& job);
    void drain(int budgetMs, const std::function<void(SampleLoadResult)>& commit);
    void shutdown();

    int total() const { return m_total; }
    int settled() const { return m_settled; }
    bool isBusy() const { return m_settled < m_total; }
    QString activeFile() const { return m_activeFile; }

private:
    struct State;

    std::shared_ptr<State> m_state;
    class QThreadPool* m_pool = nullptr;
    int m_total = 0;
    int m_settled = 0;
    QString m_activeFile;
};
