#pragma once

// Reading audio files: libsndfile for most formats, mpg123 for mp3. No OpenAL here, so this
// is safe to use from any thread and easy to test on its own.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

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
};

struct DecodedChunk {
    std::vector<short> pcmShort;
    std::vector<float> pcmFloat;
};

// What the loader thread hands to a player: the format, and either the whole file (memory)
// or the first two chunks plus where the stream carries on from (streamed).
struct DecodedAudio {
    bool ok = false;
    bool streaming = false;
    bool mp3 = false;
    bool streamEnded = false;
    std::filesystem::path path;
    std::string fileExtension; // lower case, with the dot
    int fileFormat = 0;
    std::string formatString;
    std::string subformatString;
    FormatType sampleFormat = Int16;
    int channels = 0;
    int sampleRate = 0;
    float duration = 0.0f;
    std::vector<short> pcmShort;
    std::vector<float> pcmFloat;
    std::vector<DecodedChunk> initialChunks;
    int64_t resumeFrames = 0;
    // Exact length in frames when the decoder knows it (0 when unknown).
    int64_t totalFrames = 0;
    int mp3BufferSize = 0;
};

// Min/max envelope of a whole file, one pair per bin, for waveform displays.
struct WaveformPeaks {
    bool ok = false;
    std::vector<float> mins;
    std::vector<float> maxs;
};

namespace AudioCodec {

// Sample conversions. Float audio is -1..1; 16-bit is -32768..32767.
constexpr float kShortToFloat = 1.0f / 32768.0f;
constexpr float kFloatToShort = 32767.0f;
short floatToShort(float v);

// The path as UTF-8: what mpg123 expects on every platform, and safe to log.
// path::string() can throw on Windows when the name doesn't fit the ANSI code page.
std::string pathUtf8(const std::filesystem::path& p);
// Opens any file libsndfile reads. On Windows the path goes in as UTF-16.
SNDFILE* sfOpenRead(const std::filesystem::path& path, SF_INFO* info);
// Lower-case extension with the dot (".wav").
std::string extensionOf(const std::filesystem::path& path);

// One open audio file, read in order or from a seek.
class FileReader {
public:
    FileReader() = default;
    ~FileReader() { close(); }
    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;

    // `allowFloat`: decode to float when the file has more than 16 bits. `measure`: for mp3,
    // find the exact length (scans frame headers; slow on big files without a length tag).
    bool open(const std::filesystem::path& path, bool allowFloat, bool measure = true);
    void close();
    bool isOpen() const;

    // One block, as 16-bit and float. At the end of the file it rewinds and sets ended().
    bool readChunk(std::vector<short>& shorts, std::vector<float>& floats);
    // The whole file.
    bool readAll(std::vector<short>& shorts, std::vector<float>& floats);
    // Up to `frames` interleaved float frames into `dst`; returns how many were read.
    int64_t readFloat(float* dst, int64_t frames, std::vector<short>& scratch);
    // Returns the frame it landed on.
    int64_t seek(int64_t frame);
    int64_t framePosition() const;

    bool ended() const { return m_ended; }
    bool isMp3() const { return m_mp3; }
    int channels() const { return m_channels; }
    int sampleRate() const { return m_sampleRate; }
    float duration() const { return m_duration; }
    int64_t totalFrames() const { return m_totalFrames; }
    FormatType sampleFormat() const { return m_sampleFormat; }
    int fileFormat() const { return m_fileFormat; }
    int mp3BufferSize() const;
    std::string formatString() const;
    std::string subformatString() const;
    size_t samplesRead() const { return m_samplesRead; }

private:
    std::filesystem::path m_path;
    bool m_mp3 = false;
    SNDFILE* m_snd = nullptr;
    SF_INFO m_info{};
#ifdef FEEDRA_USING_MPG123
    mpg123_handle* m_mpg = nullptr;
    int m_mp3BufferSize = 0;
#endif
    int m_channels = 0;
    int m_sampleRate = 0;
    float m_duration = 0.0f;
    FormatType m_sampleFormat = Int16;
    int m_fileFormat = 0;
    std::string m_subformat;
    size_t m_samplesRead = 0;
    bool m_ended = false;
    int64_t m_totalFrames = 0;
};

// Opens and reads a file for a player: all of it (`stream` false), or its first two chunks.
// `allowFloat` false keeps 16-bit output (devices without AL_EXT_FLOAT32).
DecodedAudio decodeFile(const std::filesystem::path& path, bool stream, bool allowFloat);

// Decodes the whole file on the calling thread (use a background thread) and returns `bins`
// min/max pairs, normalised to the loudest. Stops early, returning nothing, when `cancel` is set.
WaveformPeaks computePeaks(const std::filesystem::path& path, int bins, const std::atomic<bool>* cancel = nullptr);

} // namespace AudioCodec
