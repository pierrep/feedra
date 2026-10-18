#include "AudioCodec.h"

#include <QDebug>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#ifdef FEEDRA_USING_MPG123
#ifdef _WIN32
#include "mpg123.h"
#else
#include <mpg123.h>
#endif
#endif

namespace AudioCodec {

namespace {

// Frames per chunk read from libsndfile (mp3 uses mpg123's own block size).
constexpr int kChunkFrames = 4096;

static std::string getSoundFileFormatString(int format) {
    switch(format&SF_FORMAT_TYPEMASK)
    {
    case SF_FORMAT_WAV:
        return "Wav";
    case SF_FORMAT_AIFF:
        return "AIFF";
    case SF_FORMAT_OGG:
        return "Ogg";
    case SF_FORMAT_FLAC:
        return "FLAC";
    case SF_FORMAT_RAW:
        return "Raw";
    case 0x230000: /* SF_FORMAT_MPEG - MPEG-1/2 audio stream */
        return "Mp3";
    }

    return "Unknown format: " + std::to_string(format);
}

static std::string getSoundFileSubFormatString(int format) {

    switch(format&SF_FORMAT_SUBMASK)
    {
    case SF_FORMAT_PCM_S8:
    case SF_FORMAT_PCM_U8:
        return "8 bit PCM";
    case SF_FORMAT_PCM_16:
        return "16 bit PCM";
    case SF_FORMAT_PCM_24:
        return "24 bit PCM";
    case SF_FORMAT_PCM_32:
        return "32 bit PCM";
    case SF_FORMAT_FLOAT:
        return "Float";
    case SF_FORMAT_DOUBLE:
        return "Double";
    case SF_FORMAT_VORBIS:
        return "Vorbis";
    case SF_FORMAT_OPUS:
        return "Opus";
    case SF_FORMAT_ALAC_16:
        return "16 bit Apple Lossless Codec";
    case SF_FORMAT_ALAC_20:
        return "20 bit Apple Lossless Codec";
    case SF_FORMAT_ALAC_24:
        return "24 bit Apple Lossless Codec";
    case SF_FORMAT_ALAC_32:
        return "32 bit Apple Lossless Codec";
    case 0x0080/*SF_FORMAT_MPEG_LAYER_I*/:
        return "MPEG-1 Audio Layer I";
    case 0x0081/*SF_FORMAT_MPEG_LAYER_II*/:
        return "MPEG-1 Audio Layer II";
    case 0x0082/*SF_FORMAT_MPEG_LAYER_III*/:
        return "MPEG-2 Audio Layer III";
    case SF_FORMAT_IMA_ADPCM:
        return "IMA ADPCM";
    case SF_FORMAT_MS_ADPCM:
        return "Microsoft ADPCM";
    }

    return "Unknown format: " + std::to_string(format);
}

#ifdef FEEDRA_USING_MPG123
static std::string getMpg123EncodingString(int encoding) {
	switch(encoding) {
		case MPG123_ENC_16:
            return "16 bit";
#if MPG123_API_VERSION>=36
		case MPG123_ENC_24:
            return "24 bit";
#endif
		case MPG123_ENC_32:
            return "32 bit";
		case MPG123_ENC_8:
            return "8 bit";
		case MPG123_ENC_ALAW_8:
            return "ALAW 8 bit";
		case MPG123_ENC_FLOAT:
            return "Float";
		case MPG123_ENC_FLOAT_32:
            return "32 bit float";
		case MPG123_ENC_FLOAT_64:
            return "64 bit float";
		case MPG123_ENC_SIGNED:
            return "signed";
		case MPG123_ENC_SIGNED_16:
            return "16 bit signed";
#if MPG123_API_VERSION>=36
		case MPG123_ENC_SIGNED_24:
            return "24 bit signed";
#endif
		case MPG123_ENC_SIGNED_32:
            return "32 bit signed";
		case MPG123_ENC_SIGNED_8:
            return "8 bit signed";
		case MPG123_ENC_ULAW_8:
            return "ULAW 8 bit";
		case MPG123_ENC_UNSIGNED_16:
            return "16 bit unsigned";
#if MPG123_API_VERSION>=36
		case MPG123_ENC_UNSIGNED_24:
            return "24 bit unsigned";
#endif
		case MPG123_ENC_UNSIGNED_32:
            return "32 bit unsigned";
		case MPG123_ENC_UNSIGNED_8:
            return "8 bit unsigned";
		default:
			return "MPG123_ENC_ANY";
	}
}
#endif

// Opens any audio file for reading. On Windows the path goes in as UTF-16, so names with
// characters outside the ANSI code page (accents, CJK, ...) open too.

#ifdef FEEDRA_USING_MPG123
// Local files end where the Xing/LAME header says they end. Without this, mpg123
// keeps reading past the audio into the ID3/APE trailer and prints a resync error
// when it gives up. Quiet covers files that have no length header at all.
static void configureMpg123(mpg123_handle* handle)
{
    if (!handle) {
        return;
    }
    mpg123_param(handle, MPG123_ADD_FLAGS, MPG123_QUIET | MPG123_NO_FRANKENSTEIN, 0.0);
}

#endif

#ifdef FEEDRA_USING_MPG123
struct Mpg123Init {
    Mpg123Init() { mpg123_init(); }
    ~Mpg123Init() { mpg123_exit(); }
};
// mpg123_init() must run once before any handle is made (older libmpg123 needs it).
void ensureMpg123()
{
    static Mpg123Init init;
}
#endif

} // namespace

short floatToShort(float v)
{
    const long s = std::lrint(double(v) * kFloatToShort);
    return static_cast<short>(std::clamp<long>(s, -32768, 32767));
}

std::string pathUtf8(const std::filesystem::path& p)
{
#if defined(__cpp_char8_t)
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
#else
    return p.u8string();
#endif
}

SNDFILE* sfOpenRead(const std::filesystem::path& path, SF_INFO* info)
{
#ifdef _WIN32
    return sf_wchar_open(path.wstring().c_str(), SFM_READ, info);
#else
    return sf_open(path.c_str(), SFM_READ, info);
#endif
}

std::string extensionOf(const std::filesystem::path& path)
{
    std::string ext = pathUtf8(path.extension());
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

//------------------------------------------------------------
// FileReader
//------------------------------------------------------------
void FileReader::close()
{
    if (m_snd) {
        sf_close(m_snd);
        m_snd = nullptr;
    }
#ifdef FEEDRA_USING_MPG123
    if (m_mpg) {
        mpg123_close(m_mpg);
        mpg123_delete(m_mpg);
        m_mpg = nullptr;
    }
#endif
}

bool FileReader::isOpen() const
{
#ifdef FEEDRA_USING_MPG123
    if (m_mpg) {
        return true;
    }
#endif
    return m_snd != nullptr;
}

int FileReader::mp3BufferSize() const
{
#ifdef FEEDRA_USING_MPG123
    return m_mp3BufferSize;
#else
    return 0;
#endif
}

std::string FileReader::formatString() const
{
    return getSoundFileFormatString(m_fileFormat);
}

std::string FileReader::subformatString() const
{
    return m_mp3 ? m_subformat : getSoundFileSubFormatString(m_fileFormat);
}

bool FileReader::open(const std::filesystem::path& filePath, bool allowFloat, bool measure)
{
    close();
    m_path = filePath;
    m_ended = false;
    m_samplesRead = 0;
    m_sampleFormat = Int16;
    m_mp3 = false;
    m_fileFormat = 0;
    m_subformat.clear();
    m_totalFrames = 0;
    const std::string ext = extensionOf(filePath);
    if (ext == ".mp3") {
#ifndef FEEDRA_USING_MPG123
        qCritical() << "OpenALSoundPlayer" << "mp3 support is not built";
        return false;
#else
        ensureMpg123();
        m_mp3 = true;
        m_fileFormat = 0x230000;
        int err = MPG123_OK;
        m_mpg = mpg123_new(nullptr, &err);
        configureMpg123(m_mpg);
        if (!m_mpg || mpg123_open(m_mpg, pathUtf8(filePath).c_str()) != MPG123_OK) {
            close();
            return false;
        }
        long rate = 0;
        int encoding = 0;
        if (mpg123_getformat(m_mpg, &rate, &m_channels, &encoding) != MPG123_OK) {
            close();
            return false;
        }
        m_subformat = getMpg123EncodingString(encoding);
        if (encoding != MPG123_ENC_SIGNED_16) {
            qCritical() << "AudioCodec:" << m_subformat.c_str()
                        << "encoding for" << pathUtf8(filePath).c_str() << "unsupported";
            close();
            return false;
        }
        m_sampleRate = static_cast<int>(rate);
        m_mp3BufferSize = mpg123_outblock(m_mpg);
#if defined(MPG123_API_VERSION) && MPG123_API_VERSION >= 37
        {
            // Loop and end points are measured from the file's length, so it must be
            // exact. Files without a Xing/LAME header only have an estimate until scanned
            // (frame headers only, no decoding; this runs on the loader thread).
            long accurate = 0;
            if (measure && mpg123_getstate(m_mpg, MPG123_ACCURATE, &accurate, nullptr) == MPG123_OK && !accurate) {
                mpg123_scan(m_mpg);
            }
        }
#endif
        if (measure) {
            mpg123_seek(m_mpg, 0, SEEK_END);
            const off_t samples = mpg123_tell(m_mpg);
            m_totalFrames = samples > 0 ? static_cast<int64_t>(samples) : 0;
            m_duration = m_sampleRate > 0 ? float(samples) / float(m_sampleRate) : 0.0f;
        }
        mpg123_seek(m_mpg, 0, SEEK_SET);
        return m_channels > 0 && m_sampleRate > 0;
#endif
    }

    memset(&m_info, 0, sizeof(m_info));
    m_snd = sfOpenRead(filePath, &m_info);
    if (!m_snd) {
        return false;
    }
    m_fileFormat = m_info.format;
    m_channels = m_info.channels;
    m_sampleRate = m_info.samplerate;
    m_duration = m_info.samplerate > 0 ? float(m_info.frames) / float(m_info.samplerate) : 0.0f;
    m_totalFrames = static_cast<int64_t>(m_info.frames);
    // No SFC_CALC_SIGNAL_MAX here: it reads the whole file just to open it, and libsndfile
    // already converts float files to 16-bit on its own when that's what is read.
    const int sub = m_info.format & SF_FORMAT_SUBMASK;
    switch (sub) {
    case SF_FORMAT_PCM_24:
    case SF_FORMAT_PCM_32:
    case SF_FORMAT_FLOAT:
    case SF_FORMAT_DOUBLE:
    case SF_FORMAT_VORBIS:
    case SF_FORMAT_OPUS:
    case SF_FORMAT_ALAC_20:
    case SF_FORMAT_ALAC_24:
    case SF_FORMAT_ALAC_32:
    case 0x0080:
    case 0x0081:
    case 0x0082:
        if (allowFloat) {
            m_sampleFormat = Float;
        }
        break;
    default:
        break;
    }
    return m_channels > 0 && m_sampleRate > 0;
}

bool FileReader::readChunk(std::vector<short>& shorts, std::vector<float>& floats)
{
    m_ended = false;
#ifdef FEEDRA_USING_MPG123
    if (m_mp3) {
        int curr = m_mp3BufferSize;
        if (curr <= 0) {
            return false;
        }
        shorts.resize(static_cast<size_t>(curr));
        floats.resize(shorts.size());
        size_t done = 0;
        const int code = mpg123_read(m_mpg, reinterpret_cast<unsigned char*>(shorts.data()), static_cast<size_t>(curr) * 2, &done);
        shorts.resize(done / 2);
        floats.resize(shorts.size());
        // DONE is a clean end. RESYNC_FAIL is the same for a local file: the
        // bytes after the last frame (ID3, APE, padding) are not MPEG audio.
        if (code != MPG123_OK && code != MPG123_NEW_FORMAT) {
            // A clean end returns MPG123_DONE and is not logged. Anything else is the
            // resync failure mpg123 prints just before this read returns.
            if (code != MPG123_DONE) {
                qWarning() << "mpg123 stopped in" << pathUtf8(m_path).c_str()
                           << "at byte" << static_cast<long long>(mpg123_tell_stream(m_mpg))
                           << mpg123_plain_strerror(code);
            }
            mpg123_seek(m_mpg, 0, SEEK_SET);
            m_ended = true;
            m_samplesRead = 0;
        }
        for (int i = 0; i < static_cast<int>(shorts.size()); ++i) {
            floats[static_cast<size_t>(i)] = float(shorts[static_cast<size_t>(i)]) * kShortToFloat;
        }
        return !shorts.empty();
    }
#endif
    if (!m_snd || m_channels <= 0) {
        return false;
    }
    const int curr = kChunkFrames * m_channels;
    shorts.resize(static_cast<size_t>(curr));
    floats.resize(shorts.size());
    if (m_sampleFormat == Float) {
        const sf_count_t n = sf_read_float(m_snd, floats.data(), static_cast<sf_count_t>(floats.size()));
        m_samplesRead += static_cast<size_t>(n);
        if (n < static_cast<sf_count_t>(floats.size())) {
            floats.resize(static_cast<size_t>(n));
            shorts.resize(static_cast<size_t>(n));
            sf_seek(m_snd, 0, SEEK_SET);
            m_samplesRead = 0;
            m_ended = true;
        }
        for (int i = 0; i < static_cast<int>(floats.size()); ++i) {
            shorts[static_cast<size_t>(i)] = floatToShort(floats[static_cast<size_t>(i)]);
        }
    } else {
        const sf_count_t frames = sf_readf_short(m_snd, shorts.data(), curr / m_channels);
        m_samplesRead += static_cast<size_t>(frames * m_channels);
        if (frames < curr / m_channels) {
            shorts.resize(static_cast<size_t>(frames * m_channels));
            floats.resize(shorts.size());
            sf_seek(m_snd, 0, SEEK_SET);
            m_samplesRead = 0;
            m_ended = true;
        }
        for (int i = 0; i < static_cast<int>(shorts.size()); ++i) {
            floats[static_cast<size_t>(i)] = float(shorts[static_cast<size_t>(i)]) * kShortToFloat;
        }
    }
    return !shorts.empty();
}

bool FileReader::readAll(std::vector<short>& shorts, std::vector<float>& floats)
{
#ifdef FEEDRA_USING_MPG123
    if (m_mp3) {
        size_t done = 0;
        size_t bufferSize = static_cast<size_t>(mpg123_outblock(m_mpg));
        shorts.resize(bufferSize / 2);
        int code = MPG123_OK;
        while ((code = mpg123_read(m_mpg, reinterpret_cast<unsigned char*>(&shorts[shorts.size() - bufferSize / 2]), bufferSize, &done)) == MPG123_OK
               || code == MPG123_NEW_FORMAT) {
            shorts.resize(shorts.size() + bufferSize / 2);
        }
        shorts.resize(shorts.size() - (bufferSize / 2 - done / 2));
        floats.resize(shorts.size());
        for (int i = 0; i < static_cast<int>(shorts.size()); ++i) {
            floats[static_cast<size_t>(i)] = float(shorts[static_cast<size_t>(i)]) * kShortToFloat;
        }
        if (m_channels > 0 && m_sampleRate > 0) {
            m_duration = float(shorts.size() / static_cast<size_t>(m_channels)) / float(m_sampleRate);
        }
        return !shorts.empty();
    }
#endif
    if (!m_snd) {
        return false;
    }
    shorts.resize(static_cast<size_t>(m_info.frames * m_info.channels));
    floats.resize(shorts.size());
    if (shorts.empty()) {
        return false;
    }
    const int sub = m_info.format & SF_FORMAT_SUBMASK;
    if (sub == SF_FORMAT_FLOAT || sub == SF_FORMAT_DOUBLE) {
        const sf_count_t samplesReadNow = sf_read_float(m_snd, floats.data(), static_cast<sf_count_t>(floats.size()));
        if (samplesReadNow < static_cast<sf_count_t>(floats.size())) {
            floats.resize(static_cast<size_t>(samplesReadNow));
            shorts.resize(floats.size());
        }
        for (int i = 0; i < static_cast<int>(floats.size()); ++i) {
            shorts[static_cast<size_t>(i)] = floatToShort(floats[static_cast<size_t>(i)]);
        }
    } else {
        const sf_count_t framesRead = sf_readf_short(m_snd, shorts.data(), m_info.frames);
        if (framesRead < m_info.frames) {
            return false;
        }
        sf_seek(m_snd, 0, SEEK_SET);
        const sf_count_t floatFrames = sf_readf_float(m_snd, floats.data(), m_info.frames);
        if (floatFrames < m_info.frames) {
            return false;
        }
    }
    return !shorts.empty();
}

int64_t FileReader::framePosition() const
{
#ifdef FEEDRA_USING_MPG123
    if (m_mp3 && m_mpg) {
        return static_cast<int64_t>(mpg123_tell(m_mpg));
    }
#endif
    if (m_snd) {
        return static_cast<int64_t>(sf_seek(m_snd, 0, SEEK_CUR));
    }
    return 0;
}

int64_t FileReader::seek(int64_t frame)
{
    frame = std::max<int64_t>(0, frame);
    m_ended = false;
#ifdef FEEDRA_USING_MPG123
    if (m_mpg) {
        const off_t r = mpg123_seek(m_mpg, static_cast<off_t>(frame), SEEK_SET);
        return r >= 0 ? static_cast<int64_t>(r) : frame;
    }
#endif
    if (m_snd) {
        const sf_count_t r = sf_seek(m_snd, static_cast<sf_count_t>(frame), SEEK_SET);
        return r >= 0 ? static_cast<int64_t>(r) : frame;
    }
    return frame;
}

int64_t FileReader::readFloat(float* dst, int64_t frames, std::vector<short>& scratch)
{
    if (frames <= 0 || m_channels <= 0) {
        return 0;
    }
    const size_t samples = static_cast<size_t>(frames) * static_cast<size_t>(m_channels);
    int64_t got = 0;
#ifdef FEEDRA_USING_MPG123
    if (m_mpg) {
        if (scratch.size() < samples) {
            scratch.resize(samples);
        }
        // Fill from what mpg123 actually produced: a format notice or short read must never
        // leave stale samples behind.
        const size_t want = samples * 2;
        size_t filled = 0;
        while (filled < want) {
            size_t done = 0;
            const int code = mpg123_read(m_mpg, reinterpret_cast<unsigned char*>(scratch.data()) + filled, want - filled, &done);
            filled += done;
            if (code == MPG123_NEW_FORMAT) {
                continue;
            }
            if (code != MPG123_OK || done == 0) {
                break;
            }
        }
        got = static_cast<int64_t>(filled / 2 / static_cast<size_t>(m_channels));
        const size_t count = static_cast<size_t>(got) * static_cast<size_t>(m_channels);
        for (size_t i = 0; i < count; ++i) {
            dst[i] = float(scratch[i]) * kShortToFloat;
        }
        return got;
    }
#endif
    if (!m_snd) {
        return 0;
    }
    if (m_sampleFormat == Float) {
        got = static_cast<int64_t>(sf_readf_float(m_snd, dst, static_cast<sf_count_t>(frames)));
    } else {
        if (scratch.size() < samples) {
            scratch.resize(samples);
        }
        got = static_cast<int64_t>(sf_readf_short(m_snd, scratch.data(), static_cast<sf_count_t>(frames)));
        const size_t count = static_cast<size_t>(std::max<int64_t>(0, got)) * static_cast<size_t>(m_channels);
        for (size_t i = 0; i < count; ++i) {
            dst[i] = float(scratch[i]) * kShortToFloat;
        }
    }
    return std::max<int64_t>(0, got);
}

//------------------------------------------------------------
// decodeFile / computePeaks
//------------------------------------------------------------
DecodedAudio decodeFile(const std::filesystem::path& fileName, bool isStream, bool allowFloat)
{
    DecodedAudio out;
    out.path = fileName;
    out.streaming = isStream;
    out.fileExtension = extensionOf(fileName);

    FileReader stream;
    if (!stream.open(fileName, allowFloat)) {
        qCritical() << "AudioCodec: couldn't read" << pathUtf8(fileName).c_str();
        return out;
    }

    out.mp3 = stream.isMp3();
    out.channels = stream.channels();
    out.sampleRate = stream.sampleRate();
    out.duration = stream.duration();
    out.sampleFormat = stream.sampleFormat();
    out.fileFormat = stream.fileFormat();
    out.mp3BufferSize = stream.mp3BufferSize();
    out.totalFrames = stream.totalFrames();
    out.formatString = stream.formatString();
    out.subformatString = stream.subformatString();

    if (!isStream) {
        if (!stream.readAll(out.pcmShort, out.pcmFloat)) {
            qCritical() << "Sound file load failed - wrong file type or empty file";
            return out;
        }
        out.duration = stream.duration();
        if (out.channels > 0) {
            out.totalFrames = static_cast<int64_t>(std::max(out.pcmShort.size(), out.pcmFloat.size()) / static_cast<size_t>(out.channels));
        }
        out.ok = true;
        return out;
    }

    out.initialChunks.resize(2);
    if (!stream.readChunk(out.initialChunks[0].pcmShort, out.initialChunks[0].pcmFloat)) {
        qCritical() << "Sound file load failed - wrong file type or empty file";
        return out;
    }
    stream.readChunk(out.initialChunks[1].pcmShort, out.initialChunks[1].pcmFloat);
    out.streamEnded = stream.ended();
    out.resumeFrames = stream.framePosition();
    out.duration = stream.duration();
    out.ok = true;
    return out;
}

WaveformPeaks computePeaks(const std::filesystem::path& fileName, int bins, const std::atomic<bool>* cancel)
{
    WaveformPeaks out;
    if (bins <= 0) {
        return out;
    }
    // A private decoder: separate file handle, nothing shared with any playing source.
    FileReader stream;
    if (!stream.open(fileName, true)) {
        return out;
    }
    const int channels = stream.channels();
    int64_t expected = stream.totalFrames();
    if (expected <= 0) {
        expected = static_cast<int64_t>(std::llround(double(stream.duration()) * stream.sampleRate()));
    }
    if (channels <= 0 || expected <= 0) {
        return out;
    }
    out.mins.assign(static_cast<size_t>(bins), 0.0f);
    out.maxs.assign(static_cast<size_t>(bins), 0.0f);
    std::vector<unsigned char> touched(static_cast<size_t>(bins), 0);
    std::vector<short> shorts;
    std::vector<float> floats;
    int64_t frame = 0;
    // Guards against decoders that never report the end of the file.
    const int64_t frameLimit = expected * 2 + 1;

    while (frame < frameLimit) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            return WaveformPeaks{};
        }
        if (!stream.readChunk(shorts, floats)) {
            break;
        }
        const int64_t frames = static_cast<int64_t>(floats.size()) / channels;
        for (int64_t f = 0; f < frames; ++f, ++frame) {
            const int64_t bin64 = std::min<int64_t>(bins - 1, frame * bins / expected);
            const size_t bin = static_cast<size_t>(bin64);
            float lo = out.mins[bin];
            float hi = out.maxs[bin];
            if (!touched[bin]) {
                lo = 1.0f;
                hi = -1.0f;
                touched[bin] = 1;
            }
            const float* sample = &floats[static_cast<size_t>(f * channels)];
            for (int c = 0; c < channels; ++c) {
                lo = std::min(lo, sample[c]);
                hi = std::max(hi, sample[c]);
            }
            out.mins[bin] = lo;
            out.maxs[bin] = hi;
        }
        if (stream.ended()) {
            break;
        }
    }
    if (frame <= 0) {
        return WaveformPeaks{};
    }
    // Normalise so quiet files are still readable; clipping files stay at full height.
    float peak = 0.0f;
    for (int i = 0; i < bins; ++i) {
        peak = std::max(peak, std::max(std::fabs(out.mins[static_cast<size_t>(i)]), std::fabs(out.maxs[static_cast<size_t>(i)])));
    }
    if (peak > 1e-6f) {
        const float gain = 1.0f / peak;
        for (int i = 0; i < bins; ++i) {
            out.mins[static_cast<size_t>(i)] *= gain;
            out.maxs[static_cast<size_t>(i)] *= gain;
        }
    }
    out.ok = true;
    return out;
}


} // namespace AudioCodec
