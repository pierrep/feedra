#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <map>
#include <set>
#include <cstdint>

#if defined(__APPLE__) && !defined(FEEDRA_OPENAL_SOFT)
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif

#include "kiss_fft.h"
#include "kiss_fftr.h"

#ifdef _WIN32
#include "sndfile.h"
#else
#include <sndfile.h>
#endif

#ifdef FEEDRA_USING_MPG123
typedef struct mpg123_handle_struct mpg123_handle;
#endif

enum FormatType {
    Int16,
    Float,
    IMA4,
    MSADPCM
};

struct DecodedChunk {
    std::vector<short> pcmShort;
    std::vector<float> pcmFloat;
};

struct DecodedAudio {
    bool ok = false;
    bool streaming = false;
    bool mp3 = false;
    bool streamEnded = false;
    std::filesystem::path path;
    std::string fileExtension;
    int fileFormat = 0;
    std::string formatString;
    std::string subformatString;
    FormatType sampleFormat = Int16;
    int channels = 0;
    int sampleRate = 0;
    float duration = 0.0f;
    double streamScale = 1.0;
    std::vector<short> pcmShort;
    std::vector<float> pcmFloat;
    std::vector<DecodedChunk> initialChunks;
    int64_t resumeFrames = 0;
    int64_t streamSamplesRead = 0;
    int mp3BufferSize = 0;
};

void OpenALSoundUpdate();

class OpenALSoundPlayer {
public:
    using PlaybackEndedCallback = std::function<void(OpenALSoundPlayer*)>;

    OpenALSoundPlayer();
    ~OpenALSoundPlayer();

    static std::string getDefaultDeviceString();
    static ALCdevice* getCurrentDevice();
    static int reopenDevice(const char* devicename);
    static int listDevices(bool printOutput = true);
    static void printExtensions(const char *header, char separator, const char *extensions);

    bool load(const std::filesystem::path& fileName, bool stream = false);
    static DecodedAudio decodeFile(const std::filesystem::path& fileName, bool stream);
    bool uploadDecoded(DecodedAudio decoded);
    void unload();
    void play();
    void stop();
    void update();
    static void updateAll();

    void setVolume(float vol);
    void setPan(float vol);
    void setSpeed(float spd);
    void setPaused(bool bP);
    void setLoop(bool bLp);
    void setMultiPlay(bool bMp);
    void setPosition(float pct);
    void setPositionMS(int ms);

    float getPosition() const;
    int getPositionMS() const;
    bool isPlaying() const;
    float getSpeed() const;
    float getPan() const;
    float getVolume() const;
    bool isPaused() const;
    bool isLoaded() const;
    bool isLooping() const;

    static void initialize();
    static void close();

    float * getSpectrum(int bands);
    static float * getSystemSpectrum(int bands);

    float getDuration() const { return duration; }
    int getSampleRate() const { return samplerate; }
    int getNumChannels() const { return channels; }
    bool isStreamEnd() const { return stream_end; }
    float getReverbSend() const { return reverbSend; }
    void setReverbSend(float send) { reverbSend = send; }
    float getReverbSend2() const { return reverbSend2; }
    void setReverbSend2(float send) { reverbSend2 = send; }

    static int reverbPresetCount();
    static std::string reverbPresetId(int index);
    static std::string reverbPresetLabel(int index);
    static int reverbPresetIndex();
    static void setReverbPreset(int index);
    static bool setReverbPresetById(const std::string& id);

    static float defaultConvolutionGain();
    static float convolutionGain();
    static void setConvolutionGain(float gain);
    static bool convolutionAvailable();
    static std::filesystem::path convolutionImpulsePath();
    static bool setConvolutionImpulse(const std::filesystem::path& path);

    int getFileFormat() const { return fileformat; }
    ALenum getOpenALFormat() const { return openALformat; }
    std::string getFormatString() const { return format_string; }
    std::string getSubFormatString() const { return subformat_string; }

    int getNumSources() { return static_cast<int>(sources.size()); }
    bool isSpatialisedStereo() { return spatialisedStereo; }
    void setSpatialisedStereo(bool val);

    static void addPlaybackEndedListener(void* owner, PlaybackEndedCallback cb);
    static void removePlaybackEndedListener(void* owner);

    OpenALSoundPlayer* playerPtr = nullptr;

private:
    void threadedFunction();
    void startThread();
    void stopThread();
    void waitForThread();
    bool isThreadRunning() const;
    void sleepMs(int ms);

    void initFFT(int bands);
    float * getCurrentBufferSum(int size);

    static void createWindow(int size);
    static void runWindow(std::vector<float> & signal);
    static void initSystemFFT(int bands);
    static void notifyPlaybackEnded(OpenALSoundPlayer* player);

    bool attachDecodedStream(const DecodedAudio& decoded);
    void rebuildFftBuffers();

    bool sfReadFile(const std::filesystem::path& path);
    bool sfStream(const std::filesystem::path& path);
#ifdef FEEDRA_USING_MPG123
    bool mpg123ReadFile(const std::filesystem::path& path);
    bool mpg123Stream(const std::filesystem::path& path);
#endif

    size_t readFile(const std::filesystem::path& fileName);
    size_t stream(const std::filesystem::path& fileName);

    std::thread worker;
    std::atomic<bool> threadRunning{false};
    mutable std::mutex mutex;

    bool isStreaming = true;
    bool bMultiPlay = false;
    bool bLoop = false;
    bool bLoadedOk = false;
    bool bPaused = false;
    float pan = 0.0f;
    float volume = 1.0f;
    float internalFreq = 44100.0f;
    float speed = 1.0f;
    unsigned int length = 0;

    static std::vector<float> window;
    static float windowSum;

    int channels = 0;
    float duration = 0.0f;
    int samplerate = 0;
    std::filesystem::path fileName;
    std::string file_extension;
    std::vector<ALuint> buffers;
    std::vector<ALuint> sources;

    std::vector<std::vector<float> > fftBuffers;
    kiss_fftr_cfg fftCfg = nullptr;
    std::vector<float> windowedSignal;
    std::vector<float> bins;
    std::vector<kiss_fft_cpx> cx_out;

    static kiss_fftr_cfg systemFftCfg;
    static std::vector<float> systemWindowedSignal;
    static std::vector<float> systemBins;
    static std::vector<kiss_fft_cpx> systemCx_out;

    SNDFILE* streamf = nullptr;
    ALint byteblockalign = 0;
    ALint splblockalign = 0;
    size_t stream_samples_read = 0;
#ifdef FEEDRA_USING_MPG123
    mpg123_handle * mp3streamf = nullptr;
    int stream_encoding = 0;
#endif
    int mp3_buffer_size = 0;
    int fileformat = 0;
    std::string format_string;
    std::string subformat_string;
    ALenum openALformat = AL_NONE;
    enum FormatType sample_format = Int16;
    double stream_scale = 1.0;
    std::vector<short> buffer_short;
    std::vector<float> buffer_float;

    std::atomic<bool> stream_end{false};

    bool spatialisedStereo = false;

    ALuint filters[2] = { 0, 0 };
    float reverbSend = 0.0f;
    float reverbSend2 = 0.0f;
    bool bUseFilter = false;
};
