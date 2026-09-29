#include "OpenALSoundPlayer.h"
#include <QDebug>
#include <sndfile.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <set>
#include <map>
#include <thread>
#include <chrono>
#include "AL/alext.h"
#include "AL/efx.h"
#include "AL/efx-presets.h"
#include <climits>

#ifdef FEEDRA_USING_MPG123
#ifdef _WIN32
#include "mpg123.h"
#else 
#include <mpg123.h>
#endif
#endif

using namespace std;

static ALCdevice * alDevice = nullptr;
static ALCcontext * alContext = nullptr;
static std::atomic<bool> g_alFloat32{false};

#ifndef AL_SOFT_convolution_effect
#define AL_SOFT_convolution_effect
#define AL_EFFECT_CONVOLUTION_SOFT 0xA000
#endif

// Effect buses. Bus 0 and 1 are EAX reverbs (each with its own preset), bus 2 and 3 are
// convolution reverbs (each with its own impulse response). Every source has one aux send
// per bus, numbered the same, so a pad's four send levels map straight onto them.
static constexpr int kEaxBuses = OpenALSoundPlayer::kEaxReverbCount;
static constexpr int kBusCount = OpenALSoundPlayer::kSendCount;
static bool bUseEffects = false;
static bool bUseConvolution = false;
static int g_slotCount = 0;
static int g_sendCount = 0; // aux sends each source actually has
static ALuint irBuffer[OpenALSoundPlayer::kConvolutionCount] = { 0, 0 };
static float g_convolutionGain[OpenALSoundPlayer::kConvolutionCount] = { 1.0f / 16.0f, 1.0f / 16.0f };
static std::filesystem::path g_irPath[OpenALSoundPlayer::kConvolutionCount];
static ALuint effects[kBusCount] = { 0, 0, 0, 0 };
static ALuint effectSlots[kBusCount] = { 0, 0, 0, 0 };
static EFXEAXREVERBPROPERTIES reverbs[kEaxBuses] = {
    EFX_REVERB_PRESET_ALLEY,
    EFX_REVERB_PRESET_ALLEY
};

static std::mutex playbackEndedMutex;
static std::map<void*, OpenALSoundPlayer::PlaybackEndedCallback> playbackEndedListeners;

void OpenALSoundPlayer::addPlaybackEndedListener(void* owner, PlaybackEndedCallback cb)
{
    std::lock_guard<std::mutex> lock(playbackEndedMutex);
    playbackEndedListeners[owner] = std::move(cb);
}

void OpenALSoundPlayer::removePlaybackEndedListener(void* owner)
{
    std::lock_guard<std::mutex> lock(playbackEndedMutex);
    playbackEndedListeners.erase(owner);
}

void OpenALSoundPlayer::notifyPlaybackEnded(OpenALSoundPlayer* player)
{
    std::vector<PlaybackEndedCallback> cbs;
    {
        std::lock_guard<std::mutex> lock(playbackEndedMutex);
        for (auto& kv : playbackEndedListeners) {
            cbs.push_back(kv.second);
        }
    }
    for (auto& cb : cbs) {
        cb(player);
    }
}

vector<float> OpenALSoundPlayer::window;
float OpenALSoundPlayer::windowSum = 0.f;


kiss_fftr_cfg OpenALSoundPlayer::systemFftCfg=0;
vector<float> OpenALSoundPlayer::systemWindowedSignal;
vector<float> OpenALSoundPlayer::systemBins;
vector<kiss_fft_cpx> OpenALSoundPlayer::systemCx_out;

static set<OpenALSoundPlayer*> & players(){
	static set<OpenALSoundPlayer*> * players = new set<OpenALSoundPlayer*>;
	return *players;
}

void OpenALSoundUpdate(){
	alcProcessContext(alContext);
}

void OpenALSoundPlayer::updateAll()
{
    OpenALSoundUpdate();
    auto copy = players();
    for (auto* p : copy) {
        p->update();
    }
}

#include "AL/alext.h"

/* Filter object functions */
static LPALGENFILTERS alGenFilters;
static LPALDELETEFILTERS alDeleteFilters;
static LPALISFILTER alIsFilter;
static LPALFILTERI alFilteri;
static LPALFILTERIV alFilteriv;
static LPALFILTERF alFilterf;
static LPALFILTERFV alFilterfv;
static LPALGETFILTERI alGetFilteri;
static LPALGETFILTERIV alGetFilteriv;
static LPALGETFILTERF alGetFilterf;
static LPALGETFILTERFV alGetFilterfv;

/* Effect object functions */
static LPALGENEFFECTS alGenEffects;
static LPALDELETEEFFECTS alDeleteEffects;
static LPALISEFFECT alIsEffect;
static LPALEFFECTI alEffecti;
static LPALEFFECTIV alEffectiv;
static LPALEFFECTF alEffectf;
static LPALEFFECTFV alEffectfv;
static LPALGETEFFECTI alGetEffecti;
static LPALGETEFFECTIV alGetEffectiv;
static LPALGETEFFECTF alGetEffectf;
static LPALGETEFFECTFV alGetEffectfv;

/* Auxiliary Effect Slot object functions */
static LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots;
static LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots;
static LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot;
static LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti;
static LPALAUXILIARYEFFECTSLOTIV alAuxiliaryEffectSlotiv;
static LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf;
static LPALAUXILIARYEFFECTSLOTFV alAuxiliaryEffectSlotfv;
static LPALGETAUXILIARYEFFECTSLOTI alGetAuxiliaryEffectSloti;
static LPALGETAUXILIARYEFFECTSLOTIV alGetAuxiliaryEffectSlotiv;
static LPALGETAUXILIARYEFFECTSLOTF alGetAuxiliaryEffectSlotf;
static LPALGETAUXILIARYEFFECTSLOTFV alGetAuxiliaryEffectSlotfv;

/* OpenAL soft specific functions e.g. to get latency */
static LPALSOURCEDSOFT alSourcedSOFT;
static LPALSOURCE3DSOFT alSource3dSOFT;
static LPALSOURCEDVSOFT alSourcedvSOFT;
static LPALGETSOURCEDSOFT alGetSourcedSOFT;
static LPALGETSOURCE3DSOFT alGetSource3dSOFT;
static LPALGETSOURCEDVSOFT alGetSourcedvSOFT;
static LPALSOURCEI64SOFT alSourcei64SOFT;
static LPALSOURCE3I64SOFT alSource3i64SOFT;
static LPALSOURCEI64VSOFT alSourcei64vSOFT;
static LPALGETSOURCEI64SOFT alGetSourcei64SOFT;
static LPALGETSOURCE3I64SOFT alGetSource3i64SOFT;
static LPALGETSOURCEI64VSOFT alGetSourcei64vSOFT;

// Scheduled starts. Declared here rather than taken from alext.h so older headers still build.
#define FEEDRA_ALC_DEVICE_CLOCK_SOFT 0x1600
#define FEEDRA_AL_SAMPLE_OFFSET_CLOCK_SOFT 0x1202
typedef void (AL_APIENTRY* FeedraPlayAtTimevFn)(ALsizei n, const ALuint* sources, int64_t startTime);
typedef void (ALC_APIENTRY* FeedraGetInteger64vFn)(ALCdevice* device, ALCenum pname, ALsizei size, int64_t* values);
static FeedraPlayAtTimevFn g_playAtTimev = nullptr;
static FeedraGetInteger64vFn g_getInteger64v = nullptr;


// ----------------------------------------------------------------------------
// from http://devmaster.net/posts/2893/openal-lesson-6-advanced-loading-and-error-handles
static string getALErrorString(ALenum error) {
	switch(error) {
        case AL_NO_ERROR:
            return "AL_NO_ERROR";
        case AL_INVALID_NAME:
            return "AL_INVALID_NAME";
        case AL_INVALID_ENUM:
            return "AL_INVALID_ENUM";
        case AL_INVALID_VALUE:
            return "AL_INVALID_VALUE";
        case AL_INVALID_OPERATION:
            return "AL_INVALID_OPERATION";
        case AL_OUT_OF_MEMORY:
            return "AL_OUT_OF_MEMORY";
    };
	return "UNKWOWN_ERROR";
}

static string getALCErrorString(ALCenum  error) {
	switch(error) {
        case ALC_NO_ERROR:
            return "ALC_NO_ERROR";
        case ALC_INVALID_DEVICE:
            return "ALC_INVALID_DEVICE";
        case ALC_INVALID_CONTEXT:
            return "ALC_INVALID_CONTEXT";
        case ALC_INVALID_ENUM:
            return "ALC_INVALID_ENUM";
        case ALC_INVALID_VALUE:
            return "ALC_INVALID_VALUE";
        case ALC_OUT_OF_MEMORY:
            return "ALC_OUT_OF_MEMORY";
    };
    return "UNKNOWN_ERROR";
}

static string getOpenALFormatString(ALenum format)
{
    switch(format) {
    case AL_FORMAT_MONO16:
        return "AL_FORMAT_MONO16";
    case AL_FORMAT_MONO_FLOAT32:
        return "AL_FORMAT_MONO_FLOAT32";
    case AL_FORMAT_STEREO16:
        return "AL_FORMAT_STEREO16";
    case AL_FORMAT_STEREO_FLOAT32:
        return "AL_FORMAT_STEREO_FLOAT32";
    };
    return "Unknown OpenAL format";
}

static string getSoundFileFormatString(int format) {
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

static string getSoundFileSubFormatString(int format) {

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
static string getMpg123EncodingString(int encoding) {
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

static SNDFILE* openImpulseFile(const std::filesystem::path& filename, SF_INFO* sfinfo)
{
#ifdef _WIN32
    return sf_wchar_open(filename.wstring().c_str(), SFM_READ, sfinfo);
#else
    return sf_open(filename.string().c_str(), SFM_READ, sfinfo);
#endif
}

static ALuint loadImpulseBuffer(const std::filesystem::path& filename)
{
    SF_INFO sfinfo;
    std::memset(&sfinfo, 0, sizeof(sfinfo));
    SNDFILE* sndfile = openImpulseFile(filename, &sfinfo);
    if (!sndfile) {
        qWarning() << "Could not open impulse response" << filename.string().c_str() << sf_strerror(nullptr);
        return 0;
    }
    if (sfinfo.frames < 1 || sfinfo.frames > static_cast<sf_count_t>(INT_MAX / sizeof(float)) / sfinfo.channels) {
        qWarning() << "Bad sample count in impulse response" << filename.string().c_str() << sfinfo.frames;
        sf_close(sndfile);
        return 0;
    }

    ALenum format = AL_NONE;
    if (sfinfo.channels == 1) {
        format = AL_FORMAT_MONO_FLOAT32;
    } else if (sfinfo.channels == 2) {
        format = AL_FORMAT_STEREO_FLOAT32;
    } else if (sfinfo.channels == 3) {
        if (sf_command(sndfile, SFC_WAVEX_GET_AMBISONIC, nullptr, 0) == SF_AMBISONIC_B_FORMAT) {
            format = AL_FORMAT_BFORMAT2D_FLOAT32;
        }
    } else if (sfinfo.channels == 4) {
        if (sf_command(sndfile, SFC_WAVEX_GET_AMBISONIC, nullptr, 0) == SF_AMBISONIC_B_FORMAT) {
            format = AL_FORMAT_BFORMAT3D_FLOAT32;
        }
    }
    if (!format) {
        qWarning() << "Unsupported impulse response channel count" << sfinfo.channels;
        sf_close(sndfile);
        return 0;
    }

    qInfo() << "Loading impulse response:" << filename.string().c_str()
            << sfinfo.samplerate << "hz" << static_cast<long long>(sfinfo.frames) << "frames";

    std::vector<float> samples(static_cast<size_t>(sfinfo.frames * sfinfo.channels));
    const sf_count_t numFrames = sf_readf_float(sndfile, samples.data(), sfinfo.frames);
    sf_close(sndfile);
    if (numFrames < 1) {
        qWarning() << "Failed to read impulse response" << filename.string().c_str();
        return 0;
    }

    ALuint buffer = 0;
    alGetError();
    alGenBuffers(1, &buffer);
    const ALsizei numBytes = static_cast<ALsizei>(numFrames * sfinfo.channels) * static_cast<ALsizei>(sizeof(float));
    alBufferData(buffer, format, samples.data(), numBytes, sfinfo.samplerate);
    const ALenum err = alGetError();
    if (err != AL_NO_ERROR) {
        qWarning() << "OpenAL error loading impulse response:" << alGetString(err);
        if (buffer && alIsBuffer(buffer)) {
            alDeleteBuffers(1, &buffer);
        }
        return 0;
    }
    return buffer;
}

static ALuint createConvolutionEffect(ALuint effect)
{
    alGetError();
    alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_CONVOLUTION_SOFT);
    const ALenum err = alGetError();
    if (err != AL_NO_ERROR) {
        qWarning() << "Convolution reverb is not supported:" << alGetString(err);
        return 0;
    }
    qInfo() << "Convolution reverb effect created";
    return effect;
}

static bool applyConvolutionSlot(int which)
{
    const int bus = kEaxBuses + which;
    if (!bUseConvolution || effectSlots[bus] == 0 || effects[bus] == 0 || irBuffer[which] == 0) {
        return false;
    }
    alGetError();
    alAuxiliaryEffectSloti(effectSlots[bus], AL_BUFFER, static_cast<ALint>(irBuffer[which]));
    alAuxiliaryEffectSlotf(effectSlots[bus], AL_EFFECTSLOT_GAIN, g_convolutionGain[which]);
    alAuxiliaryEffectSloti(effectSlots[bus], AL_EFFECTSLOT_EFFECT, static_cast<ALint>(effects[bus]));
    const ALenum err = alGetError();
    if (err != AL_NO_ERROR) {
        qWarning() << "Failed to apply convolution reverb:" << alGetString(err);
        return false;
    }
    return true;
}

/* LoadEffect loads the given initial reverb properties into the given OpenAL
 * effect object, and returns non-zero on success.
 */
static int LoadEffect(ALuint effect, const EFXEAXREVERBPROPERTIES *reverb)
{
    ALenum err;

    alGetError();

    /* Prepare the effect for EAX Reverb (standard reverb doesn't contain
     * the needed panning vectors).
     */
    alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
    err = alGetError();
    if(err != AL_NO_ERROR)
    {
        fprintf(stderr, "Failed to set EAX Reverb: %s (0x%04x)\n", alGetString(err), err);
        return 0;
    }

    /* Load the reverb properties. */
    alEffectf(effect, AL_EAXREVERB_DENSITY, reverb->flDensity);
    alEffectf(effect, AL_EAXREVERB_DIFFUSION, reverb->flDiffusion);
    /* Presets bake in about −10 dB of room gain. Load them at unity so the send is the level control. */
    alEffectf(effect, AL_EAXREVERB_GAIN, 1.0f);
    alEffectf(effect, AL_EAXREVERB_GAINHF, reverb->flGainHF);
    alEffectf(effect, AL_EAXREVERB_GAINLF, reverb->flGainLF);
    alEffectf(effect, AL_EAXREVERB_DECAY_TIME, reverb->flDecayTime);
    alEffectf(effect, AL_EAXREVERB_DECAY_HFRATIO, reverb->flDecayHFRatio);
    alEffectf(effect, AL_EAXREVERB_DECAY_LFRATIO, reverb->flDecayLFRatio);
    alEffectf(effect, AL_EAXREVERB_REFLECTIONS_GAIN, reverb->flReflectionsGain);
    alEffectf(effect, AL_EAXREVERB_REFLECTIONS_DELAY, reverb->flReflectionsDelay);
    alEffectfv(effect, AL_EAXREVERB_REFLECTIONS_PAN, reverb->flReflectionsPan);
    alEffectf(effect, AL_EAXREVERB_LATE_REVERB_GAIN, reverb->flLateReverbGain);
    alEffectf(effect, AL_EAXREVERB_LATE_REVERB_DELAY, reverb->flLateReverbDelay);
    alEffectfv(effect, AL_EAXREVERB_LATE_REVERB_PAN, reverb->flLateReverbPan);
    alEffectf(effect, AL_EAXREVERB_ECHO_TIME, reverb->flEchoTime);
    alEffectf(effect, AL_EAXREVERB_ECHO_DEPTH, reverb->flEchoDepth);
    alEffectf(effect, AL_EAXREVERB_MODULATION_TIME, reverb->flModulationTime);
    alEffectf(effect, AL_EAXREVERB_MODULATION_DEPTH, reverb->flModulationDepth);
    alEffectf(effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, reverb->flAirAbsorptionGainHF);
    alEffectf(effect, AL_EAXREVERB_HFREFERENCE, reverb->flHFReference);
    alEffectf(effect, AL_EAXREVERB_LFREFERENCE, reverb->flLFReference);
    alEffectf(effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, reverb->flRoomRolloffFactor);
    alEffecti(effect, AL_EAXREVERB_DECAY_HFLIMIT, reverb->iDecayHFLimit);

    /* Check if an error occurred, and return failure if so. */
    err = alGetError();
    if(err != AL_NO_ERROR)
    {
        fprintf(stderr, "Error setting up reverb: %s\n", alGetString(err));
        return 0;
    }

    return 1;
}

namespace {

struct ReverbPreset {
    const char* id;
    EFXEAXREVERBPROPERTIES props;
};

#define FEEDRA_REVERB(name) { #name, EFX_REVERB_PRESET_##name }

const ReverbPreset kReverbPresets[] = {
    FEEDRA_REVERB(GENERIC),
    FEEDRA_REVERB(PADDEDCELL),
    FEEDRA_REVERB(ROOM),
    FEEDRA_REVERB(BATHROOM),
    FEEDRA_REVERB(LIVINGROOM),
    FEEDRA_REVERB(STONEROOM),
    FEEDRA_REVERB(AUDITORIUM),
    FEEDRA_REVERB(CONCERTHALL),
    FEEDRA_REVERB(CAVE),
    FEEDRA_REVERB(ARENA),
    FEEDRA_REVERB(HANGAR),
    FEEDRA_REVERB(CARPETEDHALLWAY),
    FEEDRA_REVERB(HALLWAY),
    FEEDRA_REVERB(STONECORRIDOR),
    FEEDRA_REVERB(ALLEY),
    FEEDRA_REVERB(FOREST),
    FEEDRA_REVERB(CITY),
    FEEDRA_REVERB(MOUNTAINS),
    FEEDRA_REVERB(QUARRY),
    FEEDRA_REVERB(PLAIN),
    FEEDRA_REVERB(PARKINGLOT),
    FEEDRA_REVERB(SEWERPIPE),
    FEEDRA_REVERB(UNDERWATER),
    FEEDRA_REVERB(DRUGGED),
    FEEDRA_REVERB(DIZZY),
    FEEDRA_REVERB(PSYCHOTIC),
    FEEDRA_REVERB(CASTLE_SMALLROOM),
    FEEDRA_REVERB(CASTLE_SHORTPASSAGE),
    FEEDRA_REVERB(CASTLE_MEDIUMROOM),
    FEEDRA_REVERB(CASTLE_LARGEROOM),
    FEEDRA_REVERB(CASTLE_LONGPASSAGE),
    FEEDRA_REVERB(CASTLE_HALL),
    FEEDRA_REVERB(CASTLE_CUPBOARD),
    FEEDRA_REVERB(CASTLE_COURTYARD),
    FEEDRA_REVERB(CASTLE_ALCOVE),
    FEEDRA_REVERB(FACTORY_SMALLROOM),
    FEEDRA_REVERB(FACTORY_SHORTPASSAGE),
    FEEDRA_REVERB(FACTORY_MEDIUMROOM),
    FEEDRA_REVERB(FACTORY_LARGEROOM),
    FEEDRA_REVERB(FACTORY_LONGPASSAGE),
    FEEDRA_REVERB(FACTORY_HALL),
    FEEDRA_REVERB(FACTORY_CUPBOARD),
    FEEDRA_REVERB(FACTORY_COURTYARD),
    FEEDRA_REVERB(FACTORY_ALCOVE),
    FEEDRA_REVERB(ICEPALACE_SMALLROOM),
    FEEDRA_REVERB(ICEPALACE_SHORTPASSAGE),
    FEEDRA_REVERB(ICEPALACE_MEDIUMROOM),
    FEEDRA_REVERB(ICEPALACE_LARGEROOM),
    FEEDRA_REVERB(ICEPALACE_LONGPASSAGE),
    FEEDRA_REVERB(ICEPALACE_HALL),
    FEEDRA_REVERB(ICEPALACE_CUPBOARD),
    FEEDRA_REVERB(ICEPALACE_COURTYARD),
    FEEDRA_REVERB(ICEPALACE_ALCOVE),
    FEEDRA_REVERB(SPACESTATION_SMALLROOM),
    FEEDRA_REVERB(SPACESTATION_SHORTPASSAGE),
    FEEDRA_REVERB(SPACESTATION_MEDIUMROOM),
    FEEDRA_REVERB(SPACESTATION_LARGEROOM),
    FEEDRA_REVERB(SPACESTATION_LONGPASSAGE),
    FEEDRA_REVERB(SPACESTATION_HALL),
    FEEDRA_REVERB(SPACESTATION_CUPBOARD),
    FEEDRA_REVERB(SPACESTATION_ALCOVE),
    FEEDRA_REVERB(WOODEN_SMALLROOM),
    FEEDRA_REVERB(WOODEN_SHORTPASSAGE),
    FEEDRA_REVERB(WOODEN_MEDIUMROOM),
    FEEDRA_REVERB(WOODEN_LARGEROOM),
    FEEDRA_REVERB(WOODEN_LONGPASSAGE),
    FEEDRA_REVERB(WOODEN_HALL),
    FEEDRA_REVERB(WOODEN_CUPBOARD),
    FEEDRA_REVERB(WOODEN_COURTYARD),
    FEEDRA_REVERB(WOODEN_ALCOVE),
    FEEDRA_REVERB(SPORT_EMPTYSTADIUM),
    FEEDRA_REVERB(SPORT_SQUASHCOURT),
    FEEDRA_REVERB(SPORT_SMALLSWIMMINGPOOL),
    FEEDRA_REVERB(SPORT_LARGESWIMMINGPOOL),
    FEEDRA_REVERB(SPORT_GYMNASIUM),
    FEEDRA_REVERB(SPORT_FULLSTADIUM),
    FEEDRA_REVERB(SPORT_STADIUMTANNOY),
    FEEDRA_REVERB(PREFAB_WORKSHOP),
    FEEDRA_REVERB(PREFAB_SCHOOLROOM),
    FEEDRA_REVERB(PREFAB_PRACTISEROOM),
    FEEDRA_REVERB(PREFAB_OUTHOUSE),
    FEEDRA_REVERB(PREFAB_CARAVAN),
    FEEDRA_REVERB(DOME_TOMB),
    FEEDRA_REVERB(PIPE_SMALL),
    FEEDRA_REVERB(DOME_SAINTPAULS),
    FEEDRA_REVERB(PIPE_LONGTHIN),
    FEEDRA_REVERB(PIPE_LARGE),
    FEEDRA_REVERB(PIPE_RESONANT),
    FEEDRA_REVERB(OUTDOORS_BACKYARD),
    FEEDRA_REVERB(OUTDOORS_ROLLINGPLAINS),
    FEEDRA_REVERB(OUTDOORS_DEEPCANYON),
    FEEDRA_REVERB(OUTDOORS_CREEK),
    FEEDRA_REVERB(OUTDOORS_VALLEY),
    FEEDRA_REVERB(MOOD_HEAVEN),
    FEEDRA_REVERB(MOOD_HELL),
    FEEDRA_REVERB(MOOD_MEMORY),
    FEEDRA_REVERB(DRIVING_COMMENTATOR),
    FEEDRA_REVERB(DRIVING_PITGARAGE),
    FEEDRA_REVERB(DRIVING_INCAR_RACER),
    FEEDRA_REVERB(DRIVING_INCAR_SPORTS),
    FEEDRA_REVERB(DRIVING_INCAR_LUXURY),
    FEEDRA_REVERB(DRIVING_FULLGRANDSTAND),
    FEEDRA_REVERB(DRIVING_EMPTYGRANDSTAND),
    FEEDRA_REVERB(DRIVING_TUNNEL),
    FEEDRA_REVERB(CITY_STREETS),
    FEEDRA_REVERB(CITY_SUBWAY),
    FEEDRA_REVERB(CITY_MUSEUM),
    FEEDRA_REVERB(CITY_LIBRARY),
    FEEDRA_REVERB(CITY_UNDERPASS),
    FEEDRA_REVERB(CITY_ABANDONED),
    FEEDRA_REVERB(DUSTYROOM),
    FEEDRA_REVERB(CHAPEL),
    FEEDRA_REVERB(SMALLWATERROOM),
};

#undef FEEDRA_REVERB

int g_reverbIndex[kEaxBuses] = { -1, -1 };

int alleyPresetIndex()
{
    const int count = static_cast<int>(sizeof(kReverbPresets) / sizeof(kReverbPresets[0]));
    for (int i = 0; i < count; ++i) {
        if (std::strcmp(kReverbPresets[i].id, "ALLEY") == 0) {
            return i;
        }
    }
    return 0;
}

std::string titleIfUpper(const std::string& part)
{
    for (unsigned char ch : part) {
        if (std::islower(ch)) {
            return part;
        }
    }
    std::string out;
    bool cap = true;
    for (unsigned char ch : part) {
        if (ch == ' ') {
            out.push_back(' ');
            cap = true;
            continue;
        }
        out.push_back(static_cast<char>(cap ? std::toupper(ch) : std::tolower(ch)));
        cap = false;
    }
    return out;
}

std::string humanizeReverbId(std::string id)
{
    struct Rep {
        const char* from;
        const char* to;
    };
    static const Rep reps[] = {
        {"SMALLSWIMMINGPOOL", "Small Swimming Pool"},
        {"LARGESWIMMINGPOOL", "Large Swimming Pool"},
        {"CARPETEDHALLWAY", "Carpeted Hallway"},
        {"EMPTYGRANDSTAND", "Empty Grandstand"},
        {"FULLGRANDSTAND", "Full Grandstand"},
        {"SMALLWATERROOM", "Small Water Room"},
        {"STADIUMTANNOY", "Stadium Tannoy"},
        {"STONECORRIDOR", "Stone Corridor"},
        {"ROLLINGPLAINS", "Rolling Plains"},
        {"SHORTPASSAGE", "Short Passage"},
        {"PRACTISEROOM", "Practise Room"},
        {"EMPTYSTADIUM", "Empty Stadium"},
        {"DEEPCANYON", "Deep Canyon"},
        {"CONCERTHALL", "Concert Hall"},
        {"SPACESTATION", "Space Station"},
        {"SQUASHCOURT", "Squash Court"},
        {"SAINTPAULS", "St Paul's"},
        {"SCHOOLROOM", "School Room"},
        {"PADDEDCELL", "Padded Cell"},
        {"PARKINGLOT", "Parking Lot"},
        {"MEDIUMROOM", "Medium Room"},
        {"LONGPASSAGE", "Long Passage"},
        {"LIVINGROOM", "Living Room"},
        {"ICEPALACE", "Ice Palace"},
        {"FULLSTADIUM", "Full Stadium"},
        {"STONEROOM", "Stone Room"},
        {"SEWERPIPE", "Sewer Pipe"},
        {"PITGARAGE", "Pit Garage"},
        {"LARGEROOM", "Large Room"},
        {"DUSTYROOM", "Dusty Room"},
        {"SMALLROOM", "Small Room"},
        {"LONGTHIN", "Long Thin"},
        {"INCAR", "In-car"},
    };
    std::vector<Rep> ordered(std::begin(reps), std::end(reps));
    std::sort(ordered.begin(), ordered.end(), [](const Rep& a, const Rep& b) {
        return std::strlen(a.from) > std::strlen(b.from);
    });
    for (const Rep& rep : ordered) {
        const std::string from = rep.from;
        const std::string to = rep.to;
        std::size_t pos = 0;
        while ((pos = id.find(from, pos)) != std::string::npos) {
            id.replace(pos, from.size(), to);
            pos += to.size();
        }
    }

    std::string label;
    std::size_t start = 0;
    while (start <= id.size()) {
        const std::size_t cut = id.find('_', start);
        const std::string part = id.substr(start, cut == std::string::npos ? std::string::npos : cut - start);
        if (!label.empty()) {
            label += " / ";
        }
        label += titleIfUpper(part);
        if (cut == std::string::npos) {
            break;
        }
        start = cut + 1;
    }
    return label;
}

void applyReverbPreset(int index, int which)
{
    reverbs[which] = kReverbPresets[index].props;
    if (!bUseEffects || effects[which] == 0) {
        return;
    }
    if (!LoadEffect(effects[which], &reverbs[which])) {
        return;
    }
    alAuxiliaryEffectSloti(effectSlots[which], AL_EFFECTSLOT_EFFECT, static_cast<ALint>(effects[which]));
    alGetError();
}

} // namespace

int OpenALSoundPlayer::reverbPresetCount()
{
    return static_cast<int>(sizeof(kReverbPresets) / sizeof(kReverbPresets[0]));
}

std::string OpenALSoundPlayer::reverbPresetId(int index)
{
    if (index < 0 || index >= reverbPresetCount()) {
        return {};
    }
    return kReverbPresets[index].id;
}

std::string OpenALSoundPlayer::reverbPresetLabel(int index)
{
    if (index < 0 || index >= reverbPresetCount()) {
        return {};
    }
    return humanizeReverbId(kReverbPresets[index].id);
}

int OpenALSoundPlayer::reverbPresetIndex(int which)
{
    which = std::clamp(which, 0, kEaxBuses - 1);
    if (g_reverbIndex[which] < 0) {
        g_reverbIndex[which] = alleyPresetIndex();
    }
    return g_reverbIndex[which];
}

void OpenALSoundPlayer::setReverbPreset(int index, int which)
{
    if (index < 0 || index >= reverbPresetCount() || which < 0 || which >= kEaxBuses) {
        return;
    }
    g_reverbIndex[which] = index;
    applyReverbPreset(index, which);
}

bool OpenALSoundPlayer::setReverbPresetById(const std::string& id, int which)
{
    for (int i = 0; i < reverbPresetCount(); ++i) {
        if (id == kReverbPresets[i].id) {
            setReverbPreset(i, which);
            return true;
        }
    }
    return false;
}

bool OpenALSoundPlayer::sendAvailable(int bus)
{
    if (!bUseEffects || bus < 0 || bus >= kBusCount || bus >= g_sendCount || effectSlots[bus] == 0) {
        return false;
    }
    return bus < kEaxBuses || bUseConvolution;
}

float OpenALSoundPlayer::defaultConvolutionGain()
{
    return 1.0f / 16.0f;
}

float OpenALSoundPlayer::convolutionGain(int which)
{
    return g_convolutionGain[std::clamp(which, 0, kConvolutionCount - 1)];
}

void OpenALSoundPlayer::setConvolutionGain(float gain, int which)
{
    if (which < 0 || which >= kConvolutionCount) {
        return;
    }
    gain = std::clamp(gain, 0.0f, 1.0f);
    g_convolutionGain[which] = gain;
    const int bus = kEaxBuses + which;
    if (bUseConvolution && effectSlots[bus] != 0 && irBuffer[which] != 0) {
        alAuxiliaryEffectSlotf(effectSlots[bus], AL_EFFECTSLOT_GAIN, gain);
        alGetError();
    }
}

bool OpenALSoundPlayer::convolutionAvailable()
{
    return bUseConvolution;
}

std::filesystem::path OpenALSoundPlayer::convolutionImpulsePath(int which)
{
    return g_irPath[std::clamp(which, 0, kConvolutionCount - 1)];
}

bool OpenALSoundPlayer::setConvolutionImpulse(const std::filesystem::path& path, int which)
{
    if (!bUseConvolution || path.empty() || which < 0 || which >= kConvolutionCount) {
        return false;
    }
    const ALuint buffer = loadImpulseBuffer(path);
    if (!buffer) {
        return false;
    }
    const ALuint previous = irBuffer[which];
    irBuffer[which] = buffer;
    if (!applyConvolutionSlot(which)) {
        irBuffer[which] = previous;
        alDeleteBuffers(1, &buffer);
        return false;
    }
    g_irPath[which] = path;
    if (previous != 0 && previous != buffer) {
        alDeleteBuffers(1, &previous);
    }
    return true;
}

#define BUFFER_STREAM_SIZE 4096


// now, the individual sound player:
//------------------------------------------------------------
OpenALSoundPlayer::OpenALSoundPlayer(){
	bLoop 			= false;
	bLoadedOk 		= false;
	pan 			= 0.0f; // range for oF is -1 to 1,
	volume 			= 1.0f;
	internalFreq 	= 44100;
	speed 			= 1;
	bPaused 		= false;
    isStreaming		= true;
	channels		= 0;
    samplerate      = 0;
    fileName        = "";
    file_extension  = "";
	duration		= 0;
	fftCfg			= 0;
	streamf			= 0;
    spatialisedStereo = false;
    bUseFilter = false;
#ifdef FEEDRA_USING_MPG123
	mp3streamf		= 0;
#endif
	players().insert(this);
}
void OpenALSoundPlayer::startThread()
{
    if (threadRunning) {
        return;
    }
    if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) {
        worker.join();
    }
    threadRunning = true;
    worker = std::thread(&OpenALSoundPlayer::threadedFunction, this);
}

void OpenALSoundPlayer::stopThread()
{
    threadRunning = false;
}

void OpenALSoundPlayer::waitForThread()
{
    threadRunning = false;
    if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) {
        worker.join();
    }
}

bool OpenALSoundPlayer::isThreadRunning() const
{
    return threadRunning;
}

void OpenALSoundPlayer::sleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}


// ----------------------------------------------------------------------------
OpenALSoundPlayer::~OpenALSoundPlayer(){
	unload();
	kiss_fftr_free(fftCfg);
	players().erase(this);
	if( players().empty() ){
		close();
	}
    waitForThread();
}

int getDevices(const char *type, const char *list, bool printOutput)
{
  ALCchar *ptr, *nptr;
  int num_devices = 0;

  ptr = (ALCchar *)list;
  if(printOutput) qInfo() << "List of all available " << type << " devices: ";
  if (!list)
  {
    if(printOutput) qInfo() << "none";
  }
  else
  {
    nptr = ptr;
    while (*(nptr += strlen(ptr)+1) != 0)
    {
      if(printOutput) qInfo() << "* " << ptr;
      ptr = nptr;
      num_devices++;
    }
    if(printOutput) qInfo() << "* " << ptr;
    num_devices++;
  }

  return num_devices;
}

ALCdevice* OpenALSoundPlayer::getCurrentDevice()
{
    return alDevice;
}

int OpenALSoundPlayer::reopenDevice(const char* deviceName)
{
    auto ctx = alcGetCurrentContext();
    auto device = alcGetContextsDevice(ctx);
    if(device == nullptr)
    {
        return -2;
    }

    if(alcIsExtensionPresent(device, "ALC_SOFT_reopen_device"))
    {
        ALCboolean (ALC_APIENTRY*alcReopenDeviceSOFT)(ALCdevice *device, const ALCchar *name, const ALCint *attribs);
        alcReopenDeviceSOFT = reinterpret_cast<ALCboolean (ALC_APIENTRY*)(ALCdevice *device, const ALCchar *name, const ALCint *attribs)>(alcGetProcAddress(device, "alcReopenDeviceSOFT"));

        if(alcReopenDeviceSOFT(device, deviceName, NULL))
        {
            return 0;
        }

        return -3;
    }

    return -1;
}

string OpenALSoundPlayer::getDefaultDeviceString()
{
    return alcGetString(NULL, ALC_DEFAULT_ALL_DEVICES_SPECIFIER);
}

int OpenALSoundPlayer::listDevices(bool printOutput)
{
    char* devices;
    string defaultDeviceName;
    if (alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT")) {
        devices = (char *) alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER);
        defaultDeviceName = (char *) alcGetString(NULL, ALC_DEFAULT_ALL_DEVICES_SPECIFIER);
    }
    else
    {
        devices = (char *) alcGetString(NULL, ALC_DEVICE_SPECIFIER);
        defaultDeviceName = (char *) alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
    }
    int num_devices = getDevices("output",devices, printOutput);
    if(printOutput) {
        qInfo() << "Default output device name: " << defaultDeviceName.c_str();
    }

    if(printOutput)
    {
        printExtensions ("OpenAL extensions", ' ', alGetString(AL_EXTENSIONS));
        auto ctx = alcGetCurrentContext();
        auto device = alcGetContextsDevice(ctx);
        printExtensions ("ALC extensions", ' ', alcGetString(device, ALC_EXTENSIONS));
    }
    return num_devices;
}

static const int indentation = 4;
static const int maxmimumWidth = 79;
void printChar (int c, int *width)
{
  putchar (c);
  *width = (c == '\n') ? 0 : (*width + 1);
}

void indent (int *width)
{
  int i;
  for (i = 0; i < indentation; i++)
  {
    printChar (' ', width);
  }
}

void OpenALSoundPlayer::printExtensions (const char *header, char separator, const char *extensions)
{
  int width = 0, start = 0, end = 0;

  printf ("%s:\n", header);
  if (extensions == NULL || extensions[0] == '\0')
  {
    return;
  }

  indent (&width);
  while (1)
  {
    if (extensions[end] == separator || extensions[end] == '\0')
    {
      if (width + end - start + 2 > maxmimumWidth)
      {
        printChar ('\n', &width);
        indent (&width);
      }
      while (start < end)
      {
        printChar (extensions[start], &width);
        start++;
      }
      if (extensions[end] == '\0')
      {
        break;
      }
      start++;
      end++;
      if (extensions[end] == '\0')
      {
        break;
      }
      printChar (',', &width);
      printChar (' ', &width);
    }
    end++;
  }
  printChar ('\n', &width);
  fflush(stdout);
}

//---------------------------------------
// this should only be called once
void OpenALSoundPlayer::initialize(){
    if (alDevice) {
        if (alContext) {
            g_alFloat32.store(alIsExtensionPresent("AL_EXT_FLOAT32") == AL_TRUE);
        }
        return;
    }

	if( !alDevice ){

        /* C doesn't allow casting between function and non-function pointer types, so
         * with C99 we need to use a union to reinterpret the pointer type. Pre-C99
         * still needs to use a normal cast and live with the warning (C++ is fine with
         * a regular reinterpret_cast).
         */
        #if __STDC_VERSION__ >= 199901L
        #define FUNCTION_CAST(T, ptr) (union{void *p; T f;}){ptr}.f
        #elif defined(__cplusplus)
        #define FUNCTION_CAST(T, ptr) reinterpret_cast<T>(ptr)
        #else
        #define FUNCTION_CAST(T, ptr) (T)(ptr)
        #endif

        /* Define a macro to help load the function pointers. */
    #define LOAD_PROC(T, x)  ((x) = FUNCTION_CAST(T, alGetProcAddress(#x)))
        LOAD_PROC(LPALGENFILTERS, alGenFilters);
        LOAD_PROC(LPALDELETEFILTERS, alDeleteFilters);
        LOAD_PROC(LPALISFILTER, alIsFilter);
        LOAD_PROC(LPALFILTERI, alFilteri);
        LOAD_PROC(LPALFILTERIV, alFilteriv);
        LOAD_PROC(LPALFILTERF, alFilterf);
        LOAD_PROC(LPALFILTERFV, alFilterfv);
        LOAD_PROC(LPALGETFILTERI, alGetFilteri);
        LOAD_PROC(LPALGETFILTERIV, alGetFilteriv);
        LOAD_PROC(LPALGETFILTERF, alGetFilterf);
        LOAD_PROC(LPALGETFILTERFV, alGetFilterfv);

        LOAD_PROC(LPALGENEFFECTS, alGenEffects);
        LOAD_PROC(LPALDELETEEFFECTS, alDeleteEffects);
        LOAD_PROC(LPALISEFFECT, alIsEffect);
        LOAD_PROC(LPALEFFECTI, alEffecti);
        LOAD_PROC(LPALEFFECTIV, alEffectiv);
        LOAD_PROC(LPALEFFECTF, alEffectf);
        LOAD_PROC(LPALEFFECTFV, alEffectfv);
        LOAD_PROC(LPALGETEFFECTI, alGetEffecti);
        LOAD_PROC(LPALGETEFFECTIV, alGetEffectiv);
        LOAD_PROC(LPALGETEFFECTF, alGetEffectf);
        LOAD_PROC(LPALGETEFFECTFV, alGetEffectfv);

        LOAD_PROC(LPALGENAUXILIARYEFFECTSLOTS, alGenAuxiliaryEffectSlots);
        LOAD_PROC(LPALDELETEAUXILIARYEFFECTSLOTS, alDeleteAuxiliaryEffectSlots);
        LOAD_PROC(LPALISAUXILIARYEFFECTSLOT, alIsAuxiliaryEffectSlot);
        LOAD_PROC(LPALAUXILIARYEFFECTSLOTI, alAuxiliaryEffectSloti);
        LOAD_PROC(LPALAUXILIARYEFFECTSLOTIV, alAuxiliaryEffectSlotiv);
        LOAD_PROC(LPALAUXILIARYEFFECTSLOTF, alAuxiliaryEffectSlotf);
        LOAD_PROC(LPALAUXILIARYEFFECTSLOTFV, alAuxiliaryEffectSlotfv);
        LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTI, alGetAuxiliaryEffectSloti);
        LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTIV, alGetAuxiliaryEffectSlotiv);
        LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTF, alGetAuxiliaryEffectSlotf);
        LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTFV, alGetAuxiliaryEffectSlotfv);

        LOAD_PROC(LPALSOURCEDSOFT, alSourcedSOFT);
        LOAD_PROC(LPALSOURCE3DSOFT, alSource3dSOFT);
        LOAD_PROC(LPALSOURCEDVSOFT, alSourcedvSOFT);
        LOAD_PROC(LPALGETSOURCEDSOFT, alGetSourcedSOFT);
        LOAD_PROC(LPALGETSOURCE3DSOFT, alGetSource3dSOFT);
        LOAD_PROC(LPALGETSOURCEDVSOFT, alGetSourcedvSOFT);
        LOAD_PROC(LPALSOURCEI64SOFT, alSourcei64SOFT);
        LOAD_PROC(LPALSOURCE3I64SOFT, alSource3i64SOFT);
        LOAD_PROC(LPALSOURCEI64VSOFT, alSourcei64vSOFT);
        LOAD_PROC(LPALGETSOURCEI64SOFT, alGetSourcei64SOFT);
        LOAD_PROC(LPALGETSOURCE3I64SOFT, alGetSource3i64SOFT);
        LOAD_PROC(LPALGETSOURCEI64VSOFT, alGetSourcei64vSOFT);
    #undef LOAD_PROC

        ALCint major, minor;
		alDevice = alcOpenDevice( nullptr );
		if( !alDevice ){
			qCritical() << "OpenALSoundPlayer" << "initialize(): couldn't open OpenAL default device";
			return;
        }else{            
            qInfo() << "OpenALSoundPlayer" << "initialize(): opening "<< alcGetString( alDevice, ALC_DEVICE_SPECIFIER );
            alcGetIntegerv(alDevice, ALC_MAJOR_VERSION, 1, &major);
            alcGetIntegerv(alDevice, ALC_MINOR_VERSION, 1, &minor);            
		}
		// Create OpenAL context and make it current. If fails, close the OpenAL device that was just opened.
        int attrlist[] = { ALC_MAX_AUXILIARY_SENDS, 4, ALC_MONO_SOURCES, 1024, ALC_STEREO_SOURCES, 256, 0 };
        alContext = alcCreateContext( alDevice, attrlist );
		if( !alContext ){
			ALCenum err = alcGetError( alDevice ); 
			qCritical() << "OpenALSoundPlayer" << "initialize(): couldn't not create OpenAL context : "<< getALCErrorString( err ).c_str();
			close();
			return;
		}

		if( alcMakeContextCurrent( alContext )==ALC_FALSE ){
			ALCenum err = alcGetError( alDevice ); 
			qCritical() << "OpenALSoundPlayer" << "initialize(): couldn't not make current the create OpenAL context : "<< getALCErrorString( err ).c_str();
			close();
			return;
		};
		alListener3f( AL_POSITION, 0,0,0 );
        g_alFloat32.store(alIsExtensionPresent("AL_EXT_FLOAT32") == AL_TRUE);
        if (alIsExtensionPresent("AL_SOFT_source_start_delay") == AL_TRUE) {
            g_playAtTimev = reinterpret_cast<FeedraPlayAtTimevFn>(alGetProcAddress("alSourcePlayAtTimevSOFT"));
        }
        if (alcIsExtensionPresent(alDevice, "ALC_SOFT_device_clock") == ALC_TRUE) {
            g_getInteger64v = reinterpret_cast<FeedraGetInteger64vFn>(alcGetProcAddress(alDevice, "alcGetInteger64vSOFT"));
        }
        qInfo() << "Scheduled starts" << (g_playAtTimev && g_getInteger64v ? "available" : "unavailable");
#ifdef FEEDRA_USING_MPG123
		mpg123_init();
#endif

        qInfo() << "Vendor: \""<<  alGetString(AL_VENDOR) << "\"";
        qInfo() << "Renderer: \""<< alGetString(AL_RENDERER) << "\"";
        qInfo() << "Version: " << alGetString(AL_VERSION);
        qInfo() << "ALC version: " << major << "." << minor;        
        ALCint data[16];
        alcGetIntegerv(alDevice, ALC_FREQUENCY, 1, data);
        qInfo() << "Mixer sample rate: " << data[0] << " hz";
        listDevices();

        if(!alcIsExtensionPresent(alDevice, "ALC_EXT_EFX"))
        {
            qCritical() << "EFX not supported, disabling effects";
            bUseEffects = false;
        } else {
            bUseEffects = true;
            qInfo() << "EFX enabled, using effects";
        }

        if(bUseEffects) {
            int num_sends = 0;
            alcGetIntegerv(alDevice, ALC_MAX_AUXILIARY_SENDS, 1, &num_sends);
            if(alcGetError(alDevice) != ALC_NO_ERROR || num_sends < 2)
            {
                qCritical() <<  "Device does not support multiple sends (" << num_sends <<" available)";
                bUseEffects = false;
            } else {
                qInfo() << "Device supports " << num_sends <<" effect sends";

                g_sendCount = std::min(num_sends, kBusCount);

                /* Effects 0 and 1 are the two EAX reverbs, 2 and 3 the two convolution reverbs. */
                alGenEffects(kBusCount, effects);
                if(!LoadEffect(effects[0], &reverbs[0]) || !LoadEffect(effects[1], &reverbs[1]))
                {
                    qCritical( ) <<  "Failed to load effects, aborting...";
                    bUseEffects = false;
                    alDeleteEffects(kBusCount, effects);
                    for (ALuint& e : effects) {
                        e = 0;
                    }
                    close();
                    return;
                }

                bUseConvolution = createConvolutionEffect(effects[2]) != 0 && createConvolutionEffect(effects[3]) != 0;
                if (!bUseConvolution) {
                    alDeleteEffects(2, &effects[2]);
                    effects[2] = effects[3] = 0;
                }

                g_slotCount = bUseConvolution ? kBusCount : kEaxBuses;
                alGenAuxiliaryEffectSlots(g_slotCount, effectSlots);

                /* The slots copy the effect properties when an effect is attached, so
                 * changing a preset reloads the effect and attaches it again. The convolution
                 * slots get their effect once an impulse response is loaded.
                 */
                alAuxiliaryEffectSloti(effectSlots[0], AL_EFFECTSLOT_EFFECT, (ALint)effects[0]);
                alAuxiliaryEffectSloti(effectSlots[1], AL_EFFECTSLOT_EFFECT, (ALint)effects[1]);
                assert(alGetError()==AL_NO_ERROR && "Failed to set effect slot");
            }
        }

        // check max OpenAL sources
        ALCint size;
        alcGetIntegerv( alDevice, ALC_ATTRIBUTES_SIZE, 1, &size);
        std::vector<ALCint> attrs(size);
        alcGetIntegerv( alDevice, ALC_ALL_ATTRIBUTES, size, &attrs[0] );
        for(size_t i=0; i < attrs.size(); ++i)
        {
           if( attrs[i] == ALC_MONO_SOURCES )
           {
              qInfo() << "Max mono sources: " << attrs[i+1];
           }
           if( attrs[i] == ALC_STEREO_SOURCES )
           {
              qInfo() << "Max stereo sources: " << attrs[i+1];
           }
        }
//        alcGetIntegerv(alDevice, ALC_REFRESH, 1, data+1);
//        printf("refresh rate : %u hz\n", data[0]/data[1]);
	}
}

//---------------------------------------
void OpenALSoundPlayer::createWindow(int size){
	if(int(window.size())!=size){
		windowSum = 0;
		window.resize(size);
		// hanning window
		for(int i = 0; i < size; i++){
			window[i] = .54 - .46 * cos((6.28318530717958647692f * i) / (size - 1));
			windowSum += window[i];
		}
	}
}

//---------------------------------------
void OpenALSoundPlayer::close(){
	// Destroy the OpenAL context (if any) before closing the device
	if( alDevice ){
		if( alContext ){
#ifdef FEEDRA_USING_MPG123
			mpg123_exit();
#endif
            if(bUseEffects) {
                if (g_slotCount > 0) {
                    alDeleteAuxiliaryEffectSlots(g_slotCount, effectSlots);
                    alDeleteEffects(g_slotCount, effects);
                }
                for (int i = 0; i < kConvolutionCount; ++i) {
                    if (irBuffer[i] != 0) {
                        alDeleteBuffers(1, &irBuffer[i]);
                        irBuffer[i] = 0;
                    }
                    g_irPath[i].clear();
                }
                for (int i = 0; i < kBusCount; ++i) {
                    effects[i] = 0;
                    effectSlots[i] = 0;
                }
                g_sendCount = 0;
                bUseEffects = false;
                bUseConvolution = false;
                g_slotCount = 0;
            }

			alcMakeContextCurrent(nullptr);
			alcDestroyContext(alContext);
			g_playAtTimev = nullptr;
			g_getInteger64v = nullptr;
			alContext = nullptr;
		}
		if( alcCloseDevice( alDevice )==ALC_FALSE ){
			qInfo() << "OpenALSoundPlayer" << "initialize(): error closing OpenAL device.";
		}
		alDevice = nullptr;
	}
}

// ----------------------------------------------------------------------------
bool OpenALSoundPlayer::sfReadFile(const std::filesystem::path& path){
	SF_INFO sfInfo;
	SNDFILE* f = sf_open(path.string().c_str(),SFM_READ,&sfInfo);
	if(!f){
		qCritical() << "OpenALSoundPlayer" << "sfReadFile(): couldn't read \"" << path.string().c_str() << "\"";
		return false;
	}

    buffer_short.resize(sfInfo.frames*sfInfo.channels);
    buffer_float.resize(sfInfo.frames*sfInfo.channels);

	int subformat = sfInfo.format & SF_FORMAT_SUBMASK ;
	if (subformat == SF_FORMAT_FLOAT || subformat == SF_FORMAT_DOUBLE){
		double	scale ;
		sf_command (f, SFC_CALC_SIGNAL_MAX, &scale, sizeof (scale)) ;
		if (scale < 1e-10)
			scale = 1.0 ;
		else
			scale = 32700.0 / scale ;

        sf_count_t samples_read = sf_read_float (f, &buffer_float[0], buffer_float.size());
        if(samples_read<(int)buffer_float.size()){
			qWarning() << "OpenALSoundPlayer" << "sfReadFile(): read " << samples_read << " float samples, expected "
            << buffer_float.size() << " for \"" << path.string().c_str() << "\"";
		}
        for (int i = 0 ; i < int(buffer_float.size()) ; i++){
            //buffer_float[i] *= scale ;
            buffer_short[i] = 32565.0 * buffer_float[i] * scale;
		}
	}else{
        sf_count_t frames_read = sf_readf_short(f,&buffer_short[0],sfInfo.frames);
		if(frames_read<sfInfo.frames){
			qCritical() << "OpenALSoundPlayer" << "sfReadFile(): read " << frames_read << " frames from buffer, expected "
			<< sfInfo.frames << " for \"" << path.string().c_str() << "\"";
			return false;
		}
		sf_seek(f,0,SEEK_SET);
        frames_read = sf_readf_float(f,&buffer_float[0],sfInfo.frames);
		if(frames_read<sfInfo.frames){
			qCritical() << "OpenALSoundPlayer" << "sfReadFile(): read " << frames_read << " frames from fft buffer, expected "
			<< sfInfo.frames << " for \"" << path.string().c_str() << "\"";
			return false;
		}
	}
	sf_close(f);

	channels = sfInfo.channels;
	duration = float(sfInfo.frames) / float(sfInfo.samplerate);
	samplerate = sfInfo.samplerate;
	return true;
}

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

//------------------------------------------------------------
bool OpenALSoundPlayer::mpg123ReadFile(const std::filesystem::path& path){
	int err = MPG123_OK;
	mpg123_handle * f = mpg123_new(nullptr,&err);
	configureMpg123(f);
	if(mpg123_open(f,path.string().c_str())!=MPG123_OK){
		qCritical() << "OpenALSoundPlayer" << "mpg123ReadFile(): couldn't read \"" << path.string().c_str() << "\"";
		return false;
	}

	mpg123_enc_enum encoding;
	long int rate;
	mpg123_getformat(f,&rate,&channels,(int*)&encoding);
    subformat_string = getMpg123EncodingString(encoding);
	if(encoding!=MPG123_ENC_SIGNED_16){
		qCritical() << "OpenALSoundPlayer" << "mpg123ReadFile(): " << getMpg123EncodingString(encoding).c_str()
			<< " encoding for \"" << path.string().c_str() << "\"" << " unsupported, expecting MPG123_ENC_SIGNED_16";
		return false;
	}
	samplerate = rate;

	size_t done=0;
	size_t buffer_size = mpg123_outblock( f );
    buffer_short.resize(buffer_size/2);
    int code = MPG123_OK;
    while ((code = mpg123_read(f,(unsigned char*)&buffer_short[buffer_short.size()-buffer_size/2],buffer_size,&done)) == MPG123_OK
           || code == MPG123_NEW_FORMAT) {
        buffer_short.resize(buffer_short.size()+buffer_size/2);
	}
    buffer_short.resize(buffer_short.size()-(buffer_size/2-done/2));
	mpg123_close(f);
	mpg123_delete(f);

    buffer_float.resize(buffer_short.size());
    for(int i=0;i<(int)buffer_short.size();i++){
        buffer_float[i] = float(buffer_short[i])/32565.f;
	}
    duration = float(buffer_short.size()/channels) / float(samplerate);
	return true;
}
#endif

//------------------------------------------------------------
size_t OpenALSoundPlayer::readFile(const std::filesystem::path& fileName){
#ifdef FEEDRA_USING_MPG123
    if(file_extension !=".mp3"){
        if(!sfReadFile(fileName)) return 0;
	}else{
        if(!mpg123ReadFile(fileName)) return 0;
	}
#else
    if(!sfReadFile(fileName)) return false;
#endif
	fftBuffers.resize(channels);
    int numFrames = buffer_float.size()/channels;

	for(int i=0;i<channels;i++){
		fftBuffers[i].resize(numFrames);
		for(int j=0;j<numFrames;j++){
            fftBuffers[i][j] = buffer_float[j*channels+i];
		}
	}
    return numFrames;
}

//------------------------------------------------------------
bool OpenALSoundPlayer::uploadDecoded(DecodedAudio decoded, bool spatialise)
{
    spatialisedStereo = spatialise;
    return uploadDecoded(std::move(decoded));
}

//------------------------------------------------------------
namespace {

struct DecodeStream {
    std::filesystem::path path;
    bool mp3 = false;
    SNDFILE* snd = nullptr;
    SF_INFO info{};
#ifdef FEEDRA_USING_MPG123
    mpg123_handle* mpg = nullptr;
    int mp3BufferSize = 0;
#endif
    int channels = 0;
    int sampleRate = 0;
    float duration = 0.0f;
    double scale = 1.0;
    FormatType sampleFormat = Int16;
    int fileFormat = 0;
    std::string subformat;
    size_t samplesRead = 0;
    bool ended = false;
    int64_t totalFrames = 0;

    ~DecodeStream() { close(); }

    void close()
    {
        if (snd) {
            sf_close(snd);
            snd = nullptr;
        }
#ifdef FEEDRA_USING_MPG123
        if (mpg) {
            mpg123_close(mpg);
            mpg123_delete(mpg);
            mpg = nullptr;
        }
#endif
    }

    bool open(const std::filesystem::path& filePath, const std::string& ext, bool allowFloat)
    {
        path = filePath;
        if (ext == ".mp3") {
#ifndef FEEDRA_USING_MPG123
            qCritical() << "OpenALSoundPlayer" << "decodeFile(): mp3 support is not built";
            return false;
#else
            mp3 = true;
            fileFormat = 0x230000;
            int err = MPG123_OK;
            mpg = mpg123_new(nullptr, &err);
            configureMpg123(mpg);
            if (!mpg || mpg123_open(mpg, filePath.string().c_str()) != MPG123_OK) {
                close();
                return false;
            }
            long rate = 0;
            int encoding = 0;
            if (mpg123_getformat(mpg, &rate, &channels, &encoding) != MPG123_OK) {
                close();
                return false;
            }
            subformat = getMpg123EncodingString(encoding);
            if (encoding != MPG123_ENC_SIGNED_16) {
                qCritical() << "OpenALSoundPlayer" << "decodeFile():" << subformat.c_str()
                            << "encoding for" << filePath.string().c_str() << "unsupported";
                close();
                return false;
            }
            sampleRate = static_cast<int>(rate);
            mp3BufferSize = mpg123_outblock(mpg);
#if defined(MPG123_API_VERSION) && MPG123_API_VERSION >= 37
            {
                // Loop and end points are measured from the file's length, so it must be
                // exact. Files without a Xing/LAME header only have an estimate until scanned
                // (frame headers only, no decoding; this runs on the loader thread).
                long accurate = 0;
                if (mpg123_getstate(mpg, MPG123_ACCURATE, &accurate, nullptr) == MPG123_OK && !accurate) {
                    mpg123_scan(mpg);
                }
            }
#endif
            mpg123_seek(mpg, 0, SEEK_END);
            const off_t samples = mpg123_tell(mpg);
            totalFrames = samples > 0 ? static_cast<int64_t>(samples) : 0;
            duration = sampleRate > 0 ? float(samples) / float(sampleRate) : 0.0f;
            mpg123_seek(mpg, 0, SEEK_SET);
            return channels > 0 && sampleRate > 0;
#endif
        }

        memset(&info, 0, sizeof(info));
        snd = sf_open(filePath.string().c_str(), SFM_READ, &info);
        if (!snd) {
            return false;
        }
        fileFormat = info.format;
        channels = info.channels;
        sampleRate = info.samplerate;
        duration = info.samplerate > 0 ? float(info.frames) / float(info.samplerate) : 0.0f;
        totalFrames = static_cast<int64_t>(info.frames);
        const int sub = info.format & SF_FORMAT_SUBMASK;
        if (sub == SF_FORMAT_FLOAT || sub == SF_FORMAT_DOUBLE) {
            sf_command(snd, SFC_CALC_SIGNAL_MAX, &scale, sizeof(scale));
            if (scale < 1e-10) {
                scale = 1.0;
            } else {
                scale = 32700.0 / scale;
            }
        }
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
                sampleFormat = Float;
            }
            break;
        default:
            break;
        }
        return channels > 0 && sampleRate > 0;
    }

    bool readChunk(std::vector<short>& shorts, std::vector<float>& floats)
    {
        ended = false;
#ifdef FEEDRA_USING_MPG123
        if (mp3) {
            int curr = mp3BufferSize;
            if (curr <= 0) {
                return false;
            }
            shorts.resize(static_cast<size_t>(curr));
            floats.resize(shorts.size());
            size_t done = 0;
            const int code = mpg123_read(mpg, reinterpret_cast<unsigned char*>(shorts.data()), static_cast<size_t>(curr) * 2, &done);
            shorts.resize(done / 2);
            floats.resize(shorts.size());
            // DONE is a clean end. RESYNC_FAIL is the same for a local file: the
            // bytes after the last frame (ID3, APE, padding) are not MPEG audio.
            if (code != MPG123_OK && code != MPG123_NEW_FORMAT) {
                // A clean end returns MPG123_DONE and is not logged. Anything else is the
                // resync failure mpg123 prints just before this read returns.
                if (code != MPG123_DONE) {
                    qWarning() << "mpg123 stopped in" << path.string().c_str()
                               << "at byte" << static_cast<long long>(mpg123_tell_stream(mpg))
                               << mpg123_plain_strerror(code);
                }
                mpg123_seek(mpg, 0, SEEK_SET);
                ended = true;
                samplesRead = 0;
            }
            for (int i = 0; i < static_cast<int>(shorts.size()); ++i) {
                floats[static_cast<size_t>(i)] = float(shorts[static_cast<size_t>(i)]) / 32565.f;
            }
            return !shorts.empty();
        }
#endif
        if (!snd || channels <= 0) {
            return false;
        }
        const int curr = BUFFER_STREAM_SIZE * channels;
        shorts.resize(static_cast<size_t>(curr));
        floats.resize(shorts.size());
        if (sampleFormat == Float) {
            const sf_count_t n = sf_read_float(snd, floats.data(), static_cast<sf_count_t>(floats.size()));
            samplesRead += static_cast<size_t>(n);
            if (n < static_cast<sf_count_t>(floats.size())) {
                floats.resize(static_cast<size_t>(n));
                shorts.resize(static_cast<size_t>(n));
                sf_seek(snd, 0, SEEK_SET);
                samplesRead = 0;
                ended = true;
            }
            for (int i = 0; i < static_cast<int>(floats.size()); ++i) {
                shorts[static_cast<size_t>(i)] = static_cast<short>(32565.0 * floats[static_cast<size_t>(i)] * scale);
            }
        } else {
            const sf_count_t frames = sf_readf_short(snd, shorts.data(), curr / channels);
            samplesRead += static_cast<size_t>(frames * channels);
            if (frames < curr / channels) {
                shorts.resize(static_cast<size_t>(frames * channels));
                floats.resize(shorts.size());
                sf_seek(snd, 0, SEEK_SET);
                samplesRead = 0;
                ended = true;
            }
            for (int i = 0; i < static_cast<int>(shorts.size()); ++i) {
                floats[static_cast<size_t>(i)] = float(shorts[static_cast<size_t>(i)]) / 32565.0f;
            }
        }
        return !shorts.empty();
    }

    bool readAll(std::vector<short>& shorts, std::vector<float>& floats)
    {
#ifdef FEEDRA_USING_MPG123
        if (mp3) {
            size_t done = 0;
            size_t bufferSize = static_cast<size_t>(mpg123_outblock(mpg));
            shorts.resize(bufferSize / 2);
            int code = MPG123_OK;
            while ((code = mpg123_read(mpg, reinterpret_cast<unsigned char*>(&shorts[shorts.size() - bufferSize / 2]), bufferSize, &done)) == MPG123_OK
                   || code == MPG123_NEW_FORMAT) {
                shorts.resize(shorts.size() + bufferSize / 2);
            }
            shorts.resize(shorts.size() - (bufferSize / 2 - done / 2));
            floats.resize(shorts.size());
            for (int i = 0; i < static_cast<int>(shorts.size()); ++i) {
                floats[static_cast<size_t>(i)] = float(shorts[static_cast<size_t>(i)]) / 32565.f;
            }
            if (channels > 0 && sampleRate > 0) {
                duration = float(shorts.size() / static_cast<size_t>(channels)) / float(sampleRate);
            }
            return !shorts.empty();
        }
#endif
        if (!snd) {
            return false;
        }
        shorts.resize(static_cast<size_t>(info.frames * info.channels));
        floats.resize(shorts.size());
        if (shorts.empty()) {
            return false;
        }
        const int sub = info.format & SF_FORMAT_SUBMASK;
        if (sub == SF_FORMAT_FLOAT || sub == SF_FORMAT_DOUBLE) {
            const sf_count_t samplesReadNow = sf_read_float(snd, floats.data(), static_cast<sf_count_t>(floats.size()));
            if (samplesReadNow < static_cast<sf_count_t>(floats.size())) {
                floats.resize(static_cast<size_t>(samplesReadNow));
                shorts.resize(floats.size());
            }
            for (int i = 0; i < static_cast<int>(floats.size()); ++i) {
                shorts[static_cast<size_t>(i)] = static_cast<short>(32565.0 * floats[static_cast<size_t>(i)] * scale);
            }
        } else {
            const sf_count_t framesRead = sf_readf_short(snd, shorts.data(), info.frames);
            if (framesRead < info.frames) {
                return false;
            }
            sf_seek(snd, 0, SEEK_SET);
            const sf_count_t floatFrames = sf_readf_float(snd, floats.data(), info.frames);
            if (floatFrames < info.frames) {
                return false;
            }
        }
        return !shorts.empty();
    }

    int64_t framePosition() const
    {
#ifdef FEEDRA_USING_MPG123
        if (mp3 && mpg) {
            return static_cast<int64_t>(mpg123_tell(mpg));
        }
#endif
        if (snd) {
            return static_cast<int64_t>(sf_seek(snd, 0, SEEK_CUR));
        }
        return 0;
    }
};

bool uploadPcm(ALuint buffer, ALenum format, int rate, const std::vector<short>& shorts, const std::vector<float>& floats)
{
    alGetError();
    if (format == AL_FORMAT_MONO16 || format == AL_FORMAT_STEREO16) {
        if (shorts.empty()) {
            return false;
        }
        alBufferData(buffer, format, shorts.data(), static_cast<ALsizei>(shorts.size() * 2), rate);
    } else if (format == AL_FORMAT_MONO_FLOAT32 || format == AL_FORMAT_STEREO_FLOAT32) {
        if (floats.empty()) {
            return false;
        }
        alBufferData(buffer, format, floats.data(), static_cast<ALsizei>(floats.size() * 4), rate);
    } else {
        return false;
    }
    return alGetError() == AL_NO_ERROR;
}

} // namespace

DecodedAudio OpenALSoundPlayer::decodeFile(const std::filesystem::path& fileName, bool isStream)
{
    DecodedAudio out;
    out.path = fileName;
    out.streaming = isStream;
    out.fileExtension = fileName.extension().string();
    for (char& c : out.fileExtension) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }

    DecodeStream stream;
    if (!stream.open(fileName, out.fileExtension, g_alFloat32.load())) {
        qCritical() << "OpenALSoundPlayer" << "decodeFile(): couldn't read" << fileName.string().c_str();
        return out;
    }

    out.mp3 = stream.mp3;
    out.channels = stream.channels;
    out.sampleRate = stream.sampleRate;
    out.duration = stream.duration;
    out.sampleFormat = stream.sampleFormat;
    out.fileFormat = stream.fileFormat;
    out.streamScale = stream.scale;
    out.mp3BufferSize = stream.mp3BufferSize;
    out.totalFrames = stream.totalFrames;
    out.formatString = getSoundFileFormatString(out.fileFormat);
    out.subformatString = stream.mp3 ? stream.subformat : getSoundFileSubFormatString(out.fileFormat);

    if (!isStream) {
        if (!stream.readAll(out.pcmShort, out.pcmFloat)) {
            qCritical() << "Sound file load failed - wrong file type or empty file";
            return out;
        }
        out.duration = stream.duration;
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
    out.streamEnded = stream.ended;
    out.streamSamplesRead = static_cast<int64_t>(stream.samplesRead);
    out.resumeFrames = stream.framePosition();
    out.duration = stream.duration;
    out.ok = true;
    return out;
}

WaveformPeaks OpenALSoundPlayer::computePeaks(const std::filesystem::path& fileName, int bins, const std::atomic<bool>* cancel)
{
    WaveformPeaks out;
    if (bins <= 0) {
        return out;
    }
    std::string ext = fileName.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }

    // A private decoder: separate file handle, nothing shared with any playing source.
    DecodeStream stream;
    if (!stream.open(fileName, ext, true)) {
        return out;
    }
    const int channels = stream.channels;
    int64_t expected = 0;
    if (!stream.mp3) {
        expected = static_cast<int64_t>(stream.info.frames);
    }
    if (expected <= 0) {
        expected = static_cast<int64_t>(std::llround(double(stream.duration) * stream.sampleRate));
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
        if (stream.ended) {
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

bool OpenALSoundPlayer::attachDecodedStream(const DecodedAudio& decoded)
{
#ifdef FEEDRA_USING_MPG123
    if (decoded.mp3) {
        int err = MPG123_OK;
        mp3streamf = mpg123_new(nullptr, &err);
        configureMpg123(mp3streamf);
        if (!mp3streamf || mpg123_open(mp3streamf, decoded.path.string().c_str()) != MPG123_OK) {
            qCritical() << "OpenALSoundPlayer" << "attachDecodedStream(): couldn't read" << decoded.path.string().c_str();
            if (mp3streamf) {
                mpg123_close(mp3streamf);
                mpg123_delete(mp3streamf);
                mp3streamf = nullptr;
            }
            return false;
        }
        {
            // Reading the format here consumes mpg123's "new format" notice. Otherwise the
            // first read after loading returns it with no audio, and the first chunk of the
            // first play would be stale data from the previous buffer.
            long rate = 0;
            int fmtChannels = 0;
            int encoding = 0;
            mpg123_getformat(mp3streamf, &rate, &fmtChannels, &encoding);
        }
        if (decoded.totalFrames > 0) {
            totalFrames = decoded.totalFrames;
        } else {
            const off_t len = mpg123_length(mp3streamf);
            totalFrames = len > 0 ? static_cast<int64_t>(len)
                                  : static_cast<int64_t>(std::llround(double(decoded.duration) * decoded.sampleRate));
        }
        const off_t at = mpg123_seek(mp3streamf, static_cast<off_t>(decoded.resumeFrames), SEEK_SET);
        decPos.store(at >= 0 ? static_cast<int64_t>(at) : decoded.resumeFrames);
        stream_end = decoded.streamEnded;
        return true;
    }
#endif
    SF_INFO sfInfo;
    memset(&sfInfo, 0, sizeof(sfInfo));
    streamf = sf_open(decoded.path.string().c_str(), SFM_READ, &sfInfo);
    if (!streamf) {
        qCritical() << "OpenALSoundPlayer" << "attachDecodedStream(): couldn't read" << decoded.path.string().c_str();
        return false;
    }
    totalFrames = static_cast<int64_t>(sfInfo.frames);
    if (decoded.resumeFrames > 0) {
        sf_seek(streamf, static_cast<sf_count_t>(decoded.resumeFrames), SEEK_SET);
    }
    decPos.store(decoded.resumeFrames);
    stream_samples_read = static_cast<size_t>(decoded.streamSamplesRead);
    stream_end = decoded.streamEnded;
    return true;
}

void OpenALSoundPlayer::rebuildFftBuffers()
{
    if (channels <= 0 || buffer_float.empty()) {
        return;
    }
    const int numFrames = static_cast<int>(buffer_float.size()) / channels;
    fftBuffers.resize(static_cast<size_t>(channels));
    for (int i = 0; i < channels; ++i) {
        fftBuffers[static_cast<size_t>(i)].resize(static_cast<size_t>(numFrames));
        for (int j = 0; j < numFrames; ++j) {
            fftBuffers[static_cast<size_t>(i)][static_cast<size_t>(j)] = buffer_float[static_cast<size_t>(j * channels + i)];
        }
    }
}

bool OpenALSoundPlayer::load(const std::filesystem::path& fileName, bool isStream)
{
    initialize();
    DecodedAudio decoded = decodeFile(fileName, isStream);
    if (!decoded.ok) {
        return false;
    }
    return uploadDecoded(std::move(decoded));
}

bool OpenALSoundPlayer::uploadDecoded(DecodedAudio decoded)
{
    if (!decoded.ok) {
        return false;
    }
    initialize();
    if (!sources.empty()) {
        bLoadedOk = true;
    }
    if (bLoadedOk || streamf || memoryBacked
#ifdef FEEDRA_USING_MPG123
        || mp3streamf
#endif
    ) {
        unload();
    }
    bLoadedOk = false;

    fileName = decoded.path;
    bMultiPlay = false;
    // Every sample plays through the stream queue. Samples that aren't streamed from disk
    // are fed from the decoded copy in memory, so loops and fades work the same way.
    isStreaming = true;
    memoryBacked = !decoded.streaming;
    file_extension = decoded.fileExtension;
    fileformat = decoded.fileFormat;
    format_string = decoded.formatString;
    subformat_string = decoded.subformatString;
    sample_format = decoded.sampleFormat;
    channels = decoded.channels;
    samplerate = decoded.sampleRate;
    duration = decoded.duration;
    stream_scale = decoded.streamScale;
    mp3_buffer_size = decoded.mp3BufferSize;
    stream_end = false;
    totalFrames = 0;
    decPos.store(0);

    if (channels <= 0 || samplerate <= 0) {
        qCritical() << "Sound file load failed - wrong file type or empty file";
        return false;
    }

    if (!memoryBacked) {
        if (!attachDecodedStream(decoded)) {
            return false;
        }
        if (!decoded.initialChunks.empty()) {
            buffer_short = decoded.initialChunks.back().pcmShort;
            buffer_float = decoded.initialChunks.back().pcmFloat;
        }
    } else {
        if (sample_format == Int16) {
            memShort = std::move(decoded.pcmShort);
            memFrames = static_cast<int64_t>(memShort.size()) / channels;
        } else {
            memFloat = std::move(decoded.pcmFloat);
            memFrames = static_cast<int64_t>(memFloat.size()) / channels;
        }
        if (memFrames <= 0) {
            qCritical() << "Sound file load failed - wrong file type or empty file";
            memoryBacked = false;
            return false;
        }
        totalFrames = memFrames;
        duration = float(double(memFrames) / double(samplerate));
    }
    rebuildFftBuffers();

    openALformat = AL_NONE;
    if (channels == 1) {
        spatialisedStereo = false;
        if (sample_format == Int16) {
            openALformat = AL_FORMAT_MONO16;
        } else if (sample_format == Float) {
            openALformat = AL_FORMAT_MONO_FLOAT32;
        }
    } else if (channels == 2) {
        if (sample_format == Int16) {
            openALformat = spatialisedStereo ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
        } else if (sample_format == Float) {
            openALformat = spatialisedStereo ? AL_FORMAT_MONO_FLOAT32 : AL_FORMAT_STEREO_FLOAT32;
        }
    }
    if (openALformat == AL_NONE) {
        qCritical() << "OpenALSoundPlayer" << "uploadDecoded(): unsupported channel layout for" << fileName.string().c_str();
        return false;
    }

    sources.resize(spatialisedStereo ? static_cast<size_t>(channels) : 1);
    alGetError();
    alGenSources(static_cast<ALsizei>(sources.size()), &sources[0]);
    ALenum err = alGetError();
    if (err != AL_NO_ERROR) {
        qCritical() << "OpenALSoundPlayer" << "loadSound(): couldn't generate sources for " << fileName.string().c_str() << ":"
                    << static_cast<int>(err) << getALErrorString(err).c_str();
        sources.clear();
        return false;
    }
    buffers.resize(sources.size() * 2);
    alGenBuffers(static_cast<ALsizei>(buffers.size()), &buffers[0]);

    if (sources.size() == 1) {
        alSourcef(sources[0], AL_PITCH, 1.0f);
        alSourcef(sources[0], AL_GAIN, 1.0f);
        alSourcef(sources[0], AL_ROLLOFF_FACTOR, 0.0f);
        alSourcei(sources[0], AL_SOURCE_RELATIVE, AL_TRUE);
    } else {
        for (size_t i = 0; i < sources.size(); ++i) {
            const float pos[3] = { i == 0 ? -1.0f : 1.0f, 0.0f, 0.0f };
            alSourcefv(sources[i], AL_POSITION, pos);
            alSourcef(sources[i], AL_ROLLOFF_FACTOR, 0.0f);
            alSourcei(sources[i], AL_SOURCE_RELATIVE, AL_TRUE);
        }
    }
    err = alGetError();
    if (err != AL_NO_ERROR) {
        qCritical() << "OpenALSoundPlayer" << "loadSound(): couldn't set up sources for" << fileName.string().c_str()
                    << static_cast<int>(err) << getALErrorString(err).c_str();
        return false;
    }

    if (bUseEffects && !sources.empty()) {
        // One send per effect bus, on every source (both halves of a spatialised stereo file).
        for (int bus = 0; bus < kSendCount; ++bus) {
            sends[bus] = 0.0f;
            if (!sendAvailable(bus)) {
                continue;
            }
            alGenFilters(1, &filters[bus]);
            alFilteri(filters[bus], AL_FILTER_TYPE, AL_FILTER_LOWPASS);
        }
        applySends();
        err = alGetError();
        if (err != AL_NO_ERROR) {
            qCritical() << "OpenALSoundPlayer:" << "attaching FX sends failed..."
                        << static_cast<int>(err) << getALErrorString(err).c_str();
            return false;
        }
        bUseFilter = true;
    }

    setPan(pan);

    // Fill the queue. The two chunks the loader already decoded from the start of the file
    // are used as they are when the region can't touch them (the usual case), which keeps
    // decoding off this thread. Anything else is rendered here.
    bool queued = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        xfPos = -1;
        renderEnded = false;
        endNotified = false;
        outCounter = 0;
        fadeInLen = fadeInDone = 0;
        applyPendingRegion();
        int64_t initialFrames = 0;
        for (size_t c = 0; c < std::min<size_t>(2, decoded.initialChunks.size()); ++c) {
            initialFrames += static_cast<int64_t>(decoded.initialChunks[c].pcmShort.size()) / channels;
        }
        const int64_t stop = stopFrame();
        const int64_t untouched = regLoop ? regE - xfFrames
                                          : (totalFrames > 0 && stop < totalFrames ? stop - edgeFadeFrames : stop);
        const bool useInitial = !memoryBacked && regFromStart && !decoded.streamEnded
            && decoded.initialChunks.size() >= 2 && initialFrames > 0 && initialFrames <= untouched
            && decPos.load() == initialFrames;
        if (useInitial) {
            std::lock_guard<std::mutex> hist(historyMutex);
            resetHistoryLocked();
            int64_t start = 0;
            for (size_t c = 0; c < 2; ++c) {
                const DecodedChunk& chunk = decoded.initialChunks[c];
                buffer_short = chunk.pcmShort;
                buffer_float = chunk.pcmFloat;
                const int64_t frames = static_cast<int64_t>(buffer_short.size()) / channels;
                if (sources.size() == 1) {
                    ALuint buffer = buffers[c];
                    if (fillBuffer(buffer, -1)) {
                        alSourceQueueBuffers(sources[0], 1, &buffer);
                    }
                } else {
                    for (size_t i = 0; i < sources.size(); ++i) {
                        ALuint buffer = buffers[c * sources.size() + i];
                        if (fillBuffer(buffer, static_cast<int>(i))) {
                            alSourceQueueBuffers(sources[i], 1, &buffer);
                        }
                    }
                }
                ChunkRecord rec;
                rec.outStart = start;
                rec.frames = frames;
                rec.toEnd = regLoop || stopFrame() >= INT64_MAX / 8 ? -1 : std::max<int64_t>(0, stopFrame() - (start + frames));
                addSegment(rec, start, start, frames);
                pushHistoryLocked(rec);
                start += frames;
            }
            outCounter = start;
            queued = true;
        }
        if (!queued) {
            queued = rebuildQueueLocked(-1, false);
        }
    }
    if (!queued) {
        qCritical() << "OpenALSoundPlayer" << "uploadDecoded(): nothing to play in" << fileName.string().c_str();
        return false;
    }

    streamPrimed = true;
    bLoadedOk = true;
    return bLoadedOk;
}

//------------------------------------------------------------
// Loop regions and the region renderer
//
// Every sample plays through a short queue of OpenAL buffers that is refilled from a
// "renderer" on the stream thread. The renderer reads the file (or, for samples that
// aren't streamed, the decoded copy in memory) and applies the loop region on the way:
// it stops at E, wraps from E back to S with an equal-power crossfade, and fades in or
// out where playback would otherwise start or stop mid-waveform. The UI never takes part
// in a join, so loops are sample-accurate whatever the UI thread is doing.
//------------------------------------------------------------
namespace {
constexpr double kHalfPi = 1.57079632679489661923;

void effectiveRegion(const LoopRegion& r, int rate, int64_t total,
                     int64_t& S, int64_t& E, int64_t& xf)
{
    const int64_t fileEnd = total > 0 ? total : (INT64_MAX / 4);
    S = r.start > 0.0 ? static_cast<int64_t>(std::llround(r.start * rate)) : 0;
    E = r.end > 0.0 ? static_cast<int64_t>(std::llround(r.end * rate)) : fileEnd;
    E = std::min(E, fileEnd);
    S = std::clamp<int64_t>(S, 0, std::max<int64_t>(0, E - 1));
    // Guard against degenerate regions; the Waveform tab keeps loops far longer than this.
    const int64_t minLen = std::max<int64_t>(64, rate / 100);
    if (E - S < minLen) {
        S = 0;
        E = fileEnd;
    }
    const double xfMs = std::clamp(r.crossfadeMs, 0.0, LoopRegion::kMaxCrossfadeMs);
    xf = static_cast<int64_t>(std::llround(xfMs / 1000.0 * rate));
    if (E < INT64_MAX / 8) {
        xf = std::min(xf, (E - S) / 2);
    }
    xf = std::max<int64_t>(0, xf);
}
} // namespace

void OpenALSoundPlayer::setLoopRegion(const LoopRegion& region)
{
    {
        std::lock_guard<std::mutex> lock(regionMutex);
        if (requestedRegion == region) {
            return;
        }
        requestedRegion = region;
        regionVersion.fetch_add(1);
    }
    // While playing, the stream thread applies it at its next chunk. When stopped, refill
    // the queue now so the next play starts at the (possibly new) begin frame. A paused
    // sample keeps its place and picks the change up when it resumes.
    if (bLoadedOk && !sources.empty() && !isThreadRunning()) {
        ALint state = AL_STOPPED;
        alGetSourcei(sources[0], AL_SOURCE_STATE, &state);
        if (state != AL_PAUSED && state != AL_PLAYING) {
            waitForThread();
            primeStream();
        }
    }
}

LoopRegion OpenALSoundPlayer::getLoopRegion() const
{
    std::lock_guard<std::mutex> lock(regionMutex);
    return requestedRegion;
}

int64_t OpenALSoundPlayer::getBeginFrame() const
{
    const LoopRegion r = getLoopRegion();
    if (r.playFromStart || samplerate <= 0) {
        return 0;
    }
    int64_t S = 0, E = 0, xf = 0;
    effectiveRegion(r, samplerate, totalFrames, S, E, xf);
    return S;
}

void OpenALSoundPlayer::setLoop(bool bLp)
{
    LoopRegion r = getLoopRegion();
    r.loop = bLp;
    setLoopRegion(r);
}

bool OpenALSoundPlayer::isLooping() const
{
    return getLoopRegion().loop;
}

void OpenALSoundPlayer::setMultiPlay(bool bMp)
{
    if (bMp) {
        qWarning() << "OpenALSoundPlayer" << "setMultiPlay(): not supported";
    }
    bMultiPlay = false;
}

int64_t OpenALSoundPlayer::stopFrame() const
{
    if (regLoop || !regToEnd) {
        return regE;
    }
    return totalFrames > 0 ? totalFrames : (INT64_MAX / 4);
}

void OpenALSoundPlayer::computeEffectiveRegion()
{
    effectiveRegion(activeRegion, samplerate, totalFrames, regS, regE, xfFrames);
    // Starting at S and stopping at a moved E only need a click-free edge, whatever the loop
    // crossfade is: 10 ms, or less on a very short region.
    edgeFadeFrames = std::max<int64_t>(16, samplerate / 100);
    if (regE < INT64_MAX / 8) {
        edgeFadeFrames = std::min(edgeFadeFrames, std::max<int64_t>(1, (regE - regS) / 2));
    }
    regLoop = activeRegion.loop;
    regToEnd = activeRegion.playToEnd;
    regFromStart = activeRegion.playFromStart;
}

void OpenALSoundPlayer::applyPendingRegion()
{
    if (xfPos >= 0) {
        return; // finish the crossfade in progress with the settings it started with
    }
    if (regionVersion.load() == appliedRegionVersion.load()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(regionMutex);
        activeRegion = requestedRegion;
        appliedRegionVersion.store(regionVersion.load());
    }
    computeEffectiveRegion();
    headCacheValid = false;
}

int OpenALSoundPlayer::chunkFrames() const
{
    int frames = BUFFER_STREAM_SIZE;
#ifdef FEEDRA_USING_MPG123
    if (mp3streamf && mp3_buffer_size > 0 && channels > 0) {
        frames = std::max(256, mp3_buffer_size / channels);
    }
#endif
    if (speed > 1.0f) {
        frames *= static_cast<int>(std::lround(speed));
    }
    return frames;
}

void OpenALSoundPlayer::seekDecoder(int64_t frame)
{
    frame = std::max<int64_t>(0, frame);
    if (totalFrames > 0) {
        frame = std::min(frame, totalFrames);
    }
    int64_t pos = frame;
    if (memoryBacked) {
        pos = std::min(frame, memFrames);
    }
#ifdef FEEDRA_USING_MPG123
    else if (mp3streamf) {
        const off_t r = mpg123_seek(mp3streamf, static_cast<off_t>(frame), SEEK_SET);
        pos = r >= 0 ? static_cast<int64_t>(r) : frame;
    }
#endif
    else if (streamf) {
        const sf_count_t r = sf_seek(streamf, static_cast<sf_count_t>(frame), SEEK_SET);
        pos = r >= 0 ? static_cast<int64_t>(r) : frame;
    }
    decPos.store(pos);
    stream_samples_read = static_cast<size_t>(pos) * static_cast<size_t>(std::max(1, channels));
}

int64_t OpenALSoundPlayer::readFrames(float* dst, int64_t frames)
{
    if (frames <= 0 || channels <= 0) {
        return 0;
    }
    const int ch = channels;
    const size_t samples = static_cast<size_t>(frames) * static_cast<size_t>(ch);
    int64_t got = 0;
    if (memoryBacked) {
        const int64_t pos = decPos.load();
        got = std::clamp<int64_t>(memFrames - pos, 0, frames);
        const size_t first = static_cast<size_t>(pos) * static_cast<size_t>(ch);
        const size_t count = static_cast<size_t>(got) * static_cast<size_t>(ch);
        if (!memShort.empty()) {
            for (size_t i = 0; i < count; ++i) {
                dst[i] = float(memShort[first + i]) / 32565.0f;
            }
        } else if (count > 0) {
            std::memcpy(dst, memFloat.data() + first, count * sizeof(float));
        }
    }
#ifdef FEEDRA_USING_MPG123
    else if (mp3streamf) {
        if (readScratch.size() < samples) {
            readScratch.resize(samples);
        }
        // Fill from what mpg123 actually produced: a format notice or short read must never
        // leave stale samples behind.
        const size_t want = samples * 2;
        size_t filled = 0;
        while (filled < want) {
            size_t done = 0;
            const int code = mpg123_read(mp3streamf, reinterpret_cast<unsigned char*>(readScratch.data()) + filled, want - filled, &done);
            filled += done;
            if (code == MPG123_NEW_FORMAT) {
                continue;
            }
            if (code != MPG123_OK || done == 0) {
                break;
            }
        }
        got = static_cast<int64_t>(filled / 2 / static_cast<size_t>(ch));
        const size_t count = static_cast<size_t>(got) * static_cast<size_t>(ch);
        for (size_t i = 0; i < count; ++i) {
            dst[i] = float(readScratch[i]) / 32565.0f;
        }
    }
#endif
    else if (streamf) {
        if (sample_format == Float) {
            got = static_cast<int64_t>(sf_readf_float(streamf, dst, static_cast<sf_count_t>(frames)));
        } else {
            if (readScratch.size() < samples) {
                readScratch.resize(samples);
            }
            got = static_cast<int64_t>(sf_readf_short(streamf, readScratch.data(), static_cast<sf_count_t>(frames)));
            const size_t count = static_cast<size_t>(std::max<int64_t>(0, got)) * static_cast<size_t>(ch);
            for (size_t i = 0; i < count; ++i) {
                dst[i] = float(readScratch[i]) / 32565.0f;
            }
        }
    }
    got = std::max<int64_t>(0, got);
    decPos.fetch_add(got);
    stream_samples_read = static_cast<size_t>(decPos.load()) * static_cast<size_t>(ch);
    return got;
}

void OpenALSoundPlayer::ensureHeadCache()
{
    if (headCacheValid) {
        return;
    }
    const size_t samples = static_cast<size_t>(std::max<int64_t>(0, xfFrames)) * static_cast<size_t>(channels);
    headCache.assign(samples, 0.0f);
    if (xfFrames > 0) {
        // One seek there and back, once per settings change; every later wrap reuses it.
        const int64_t back = decPos.load();
        seekDecoder(regS);
        readFrames(headCache.data(), xfFrames);
        seekDecoder(back);
    }
    headCacheValid = true;
}

void OpenALSoundPlayer::addSegment(ChunkRecord& rec, int64_t out, int64_t file, int64_t frames)
{
    if (frames <= 0) {
        return;
    }
    if (rec.segCount > 0) {
        Segment& last = rec.seg[rec.segCount - 1];
        if (last.out + last.frames == out && last.file + last.frames == file) {
            last.frames += frames;
            return;
        }
    }
    if (rec.segCount == kMaxSegments) {
        // More wraps than fit in one chunk (tiny loop, long chunk): extend the last one.
        rec.seg[kMaxSegments - 1].frames += frames;
        return;
    }
    rec.seg[rec.segCount++] = Segment{ out, file, frames };
}

void OpenALSoundPlayer::applyFadeIn(float* data, int64_t frames)
{
    const int ch = channels;
    for (int64_t f = 0; f < frames && fadeInDone < fadeInLen; ++f, ++fadeInDone) {
        const float g = float(std::sin((double(fadeInDone) + 0.5) / double(fadeInLen) * kHalfPi));
        for (int c = 0; c < ch; ++c) {
            data[f * ch + c] *= g;
        }
    }
}

int OpenALSoundPlayer::renderChunk(ChunkRecord& rec)
{
    rec = ChunkRecord{};
    rec.outStart = outCounter;
    const int want = chunkFrames();
    const int ch = channels;
    if (ch <= 0) {
        return 0;
    }
    if (work.size() < static_cast<size_t>(want) * static_cast<size_t>(ch)) {
        work.resize(static_cast<size_t>(want) * static_cast<size_t>(ch));
    }
    applyPendingRegion();

    const int64_t shortFade = std::max<int64_t>(16, samplerate / 500); // 2 ms
    // Softens an unavoidable hard cut at the end of what has been rendered so far.
    auto fadeOutTail = [&](int out) {
        const int64_t n = std::min<int64_t>(out, shortFade);
        float* p = work.data() + static_cast<size_t>(out - n) * static_cast<size_t>(ch);
        for (int64_t f = 0; f < n; ++f) {
            const float g = float(std::cos((double(f) + 0.5) / double(n) * kHalfPi));
            for (int c = 0; c < ch; ++c) {
                p[f * ch + c] *= g;
            }
        }
    };

    int out = 0;
    for (int guard = 0; out < want && !renderEnded && guard < 64; ++guard) {
        const int64_t S = regS;
        // The loop end while looping (or finishing a crossfade), otherwise where it stops.
        const int64_t E = xfPos >= 0 ? regE : stopFrame();
        const int64_t pos = decPos.load();
        float* dst = work.data() + static_cast<size_t>(out) * static_cast<size_t>(ch);

        if (xfPos >= 0) {
            // Inside the loop crossfade: the tail [E - xf, E) blends into the head [S, S + xf).
            ensureHeadCache();
            const int64_t n = std::min<int64_t>(want - out, xfFrames - xfPos);
            const int64_t got = readFrames(dst, n);
            if (got < n) {
                std::fill(dst + got * ch, dst + n * ch, 0.0f);
            }
            for (int64_t i = 0; i < n; ++i) {
                const double t = (double(xfPos + i) + 0.5) / double(xfFrames) * kHalfPi;
                const float gOut = float(std::cos(t));
                const float gIn = float(std::sin(t));
                const float* head = headCache.data() + static_cast<size_t>(xfPos + i) * static_cast<size_t>(ch);
                for (int c = 0; c < ch; ++c) {
                    dst[i * ch + c] = dst[i * ch + c] * gOut + head[c] * gIn;
                }
            }
            // The playhead shows the tail until the midpoint of the crossfade, then the head.
            const int64_t half = xfFrames / 2;
            const int64_t tailPart = std::clamp<int64_t>(half - xfPos, 0, n);
            addSegment(rec, rec.outStart + out, E - xfFrames + xfPos, tailPart);
            addSegment(rec, rec.outStart + out + tailPart, S + xfPos + tailPart, n - tailPart);
            applyFadeIn(dst, n);
            xfPos += n;
            out += static_cast<int>(n);
            if (xfPos >= xfFrames) {
                xfPos = -1;
                seekDecoder(S + xfFrames);
                applyPendingRegion();
            }
            continue;
        }

        if (pos >= E) {
            // Reached E without a crossfade: a loop with no crossfade set, a region moved to
            // before the cursor, or a file that ended before its reported length.
            // Arriving exactly at E is a clean stop (any fade-out has been applied already), and a
            // clean wrap when looping with no crossfade. Anything else is a cut that needs softening.
            const bool clean = pos == E && (!regLoop || xfFrames == 0);
            if (!clean && out > 0) {
                fadeOutTail(out);
            }
            if (regLoop) {
                seekDecoder(S);
                if (!clean) {
                    fadeInLen = shortFade;
                    fadeInDone = 0;
                }
                continue;
            }
            renderEnded = true;
            break;
        }

        int64_t limit = E;
        if (regLoop && xfFrames > 0) {
            const int64_t wrapAt = E - xfFrames;
            if (pos >= wrapAt) {
                xfPos = pos - wrapAt;
                continue;
            }
            limit = wrapAt;
        }
        const int64_t n = std::min<int64_t>(want - out, limit - pos);
        const int64_t got = readFrames(dst, n);
        if (got > 0) {
            // Fade-out before an end point that was moved in from the end of the file.
            const int64_t fadeOut = (!regLoop && totalFrames > 0 && E < totalFrames) ? edgeFadeFrames : 0;
            if (fadeOut > 0) {
                const int64_t foStart = E - fadeOut;
                for (int64_t f = std::max(pos, foStart); f < pos + got; ++f) {
                    const float g = float(std::cos((double(f - foStart) + 0.5) / double(fadeOut) * kHalfPi));
                    float* p = dst + (f - pos) * ch;
                    for (int c = 0; c < ch; ++c) {
                        p[c] *= g;
                    }
                }
            }
            applyFadeIn(dst, got);
            addSegment(rec, rec.outStart + out, pos, got);
            out += static_cast<int>(got);
        }
        if (got < n) {
            // The file ended before E.
            if (regLoop && (got > 0 || pos > S)) {
                if (out > 0) {
                    fadeOutTail(out);
                }
                seekDecoder(S);
                fadeInLen = shortFade;
                fadeInDone = 0;
                continue;
            }
            renderEnded = true;
        }
    }

    outCounter += out;
    rec.frames = out;
    if (renderEnded) {
        rec.toEnd = 0;
    } else if (regLoop || xfPos >= 0 || stopFrame() >= INT64_MAX / 8) {
        rec.toEnd = -1;
    } else {
        rec.toEnd = std::max<int64_t>(0, stopFrame() - decPos.load());
    }
    stream_end = renderEnded;
    convertRendered(out);
    return out;
}

void OpenALSoundPlayer::convertRendered(int frames)
{
    const size_t count = static_cast<size_t>(std::max(0, frames)) * static_cast<size_t>(std::max(1, channels));
    buffer_float.resize(count);
    buffer_short.resize(count);
    if (count == 0) {
        return;
    }
    std::memcpy(buffer_float.data(), work.data(), count * sizeof(float));
    for (size_t i = 0; i < count; ++i) {
        const long v = std::lrint(double(work[i]) * 32565.0);
        buffer_short[i] = static_cast<short>(std::clamp<long>(v, -32768, 32767));
    }
}

bool OpenALSoundPlayer::fillBuffer(ALuint buffer, int channelIndex)
{
    if (channelIndex < 0 || channels <= 1) {
        return uploadPcm(buffer, openALformat, samplerate, buffer_short, buffer_float);
    }
    const size_t frames = buffer_short.size() / static_cast<size_t>(channels);
    scratchShort.resize(frames);
    scratchFloat.resize(frames);
    for (size_t j = 0; j < frames; ++j) {
        const size_t src = j * static_cast<size_t>(channels) + static_cast<size_t>(channelIndex);
        scratchShort[j] = buffer_short[src];
        scratchFloat[j] = buffer_float[src];
    }
    return uploadPcm(buffer, openALformat, samplerate, scratchShort, scratchFloat);
}

//------------------------------------------------------------
// Playhead history
//------------------------------------------------------------
void OpenALSoundPlayer::resetHistoryLocked()
{
    historyCount = 0;
    historyNewest = -1;
    queuedRecords = 0;
    historyEnded = false;
}

void OpenALSoundPlayer::pushHistoryLocked(const ChunkRecord& rec)
{
    historyNewest = (historyNewest + 1) % kHistory;
    history[historyNewest] = rec;
    historyCount = std::min(historyCount + 1, kHistory);
    queuedRecords = std::min(queuedRecords + 1, historyCount);
    historyEnded = rec.toEnd == 0;
    if (rec.segCount > 0) {
        const Segment& last = rec.seg[rec.segCount - 1];
        lastFileFrame = last.file + last.frames;
    }
}

double OpenALSoundPlayer::mapOutputFrameLocked(double outFrame) const
{
    if (historyCount <= 0) {
        return -1.0;
    }
    const ChunkRecord& newest = history[historyNewest];
    if (outFrame >= double(newest.outStart + newest.frames)) {
        return double(lastFileFrame);
    }
    for (int k = 0; k < historyCount; ++k) {
        const ChunkRecord& r = history[(historyNewest - k + kHistory) % kHistory];
        if (outFrame < double(r.outStart) && k + 1 < historyCount) {
            continue;
        }
        if (r.segCount == 0) {
            return -1.0;
        }
        const double off = std::max(0.0, outFrame - double(r.outStart));
        for (int s = 0; s < r.segCount; ++s) {
            const Segment& seg = r.seg[s];
            if (off < double(seg.out - r.outStart + seg.frames) || s + 1 == r.segCount) {
                const double into = std::clamp(off - double(seg.out - r.outStart), 0.0, double(seg.frames));
                return double(seg.file) + into;
            }
        }
    }
    return -1.0;
}

//------------------------------------------------------------
// Scheduled starts
//------------------------------------------------------------
bool OpenALSoundPlayer::scheduledStartAvailable()
{
    return g_playAtTimev != nullptr && g_getInteger64v != nullptr && alGetSourcei64vSOFT != nullptr && alDevice != nullptr;
}

int64_t OpenALSoundPlayer::deviceClockNs()
{
    if (!g_getInteger64v || !alDevice) {
        return 0;
    }
    int64_t value = 0;
    g_getInteger64v(alDevice, FEEDRA_ALC_DEVICE_CLOCK_SOFT, 1, &value);
    return value;
}

bool OpenALSoundPlayer::predictEndDeviceTime(int64_t& endNs) const
{
    if (!scheduledStartAvailable() || sources.empty() || !bLoadedOk || samplerate <= 0) {
        return false;
    }
    if (regionVersion.load() != appliedRegionVersion.load()) {
        return false; // a settings change is on its way; the end isn't known yet
    }
    std::lock_guard<std::mutex> lock(historyMutex);
    if (historyCount == 0 || queuedRecords == 0) {
        return false;
    }
    const ChunkRecord& newest = history[historyNewest];
    if (newest.toEnd < 0) {
        return false;
    }
    ALint state = AL_STOPPED;
    alGetSourcei(sources[0], AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING) {
        return false;
    }
    ALint64SOFT values[2] = { 0, 0 };
    alGetSourcei64vSOFT(sources[0], FEEDRA_AL_SAMPLE_OFFSET_CLOCK_SOFT, values);
    const double offset = double(values[0]) / 4294967296.0;
    int64_t queuedFrames = 0;
    for (int k = 0; k < queuedRecords; ++k) {
        queuedFrames += history[(historyNewest - k + kHistory) % kHistory].frames;
    }
    const double remaining = std::max(0.0, double(queuedFrames) - offset) + double(newest.toEnd);
    const double rate = double(samplerate) * std::max(0.01, double(speed));
    int64_t from = static_cast<int64_t>(values[1]);
    const int64_t pending = pendingStartNs.load();
    if (pending > from) {
        from = pending; // scheduled but not started yet
    }
    endNs = from + static_cast<int64_t>(std::llround(remaining / rate * 1e9));
    return true;
}

bool OpenALSoundPlayer::playAtDeviceTime(int64_t startNs)
{
    if (sources.empty() || !bLoadedOk || !g_playAtTimev) {
        return false;
    }
    if (!streamPrimed) {
        waitForThread();
        primeStream();
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        bPaused = false;
        alGetError();
        g_playAtTimev(static_cast<ALsizei>(sources.size()), sources.data(), startNs);
        if (alGetError() != AL_NO_ERROR) {
            alSourcePlayv(static_cast<ALsizei>(sources.size()), sources.data());
        }
        pendingStartNs.store(startNs);
    }
    streamPrimed = false;
    startThread();
    return true;
}

void OpenALSoundPlayer::refillSparesLocked()
{
    // Buffers dropped after the end (see threadedFunction) go back into the queue so the
    // stream has its full two buffers again.
    const size_t group = std::max<size_t>(1, sources.size());
    while (spareBuffers.size() >= group && !renderEnded) {
        ChunkRecord rec;
        if (renderChunk(rec) <= 0) {
            break;
        }
        std::lock_guard<std::mutex> hist(historyMutex);
        for (size_t i = 0; i < group; ++i) {
            ALuint buffer = spareBuffers[i];
            fillBuffer(buffer, sources.size() > 1 ? static_cast<int>(i) : -1);
            alSourceQueueBuffers(sources[i], 1, &buffer);
        }
        spareBuffers.erase(spareBuffers.begin(), spareBuffers.begin() + static_cast<std::ptrdiff_t>(group));
        pushHistoryLocked(rec);
    }
}

bool OpenALSoundPlayer::isPlayingOut() const
{
    if (sources.empty() || !bLoadedOk || isThreadRunning()) {
        return false;
    }
    ALint state = AL_STOPPED;
    alGetSourcei(sources[0], AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

bool OpenALSoundPlayer::hasEnded() const
{
    return stream_end.load() && endNotified.load();
}

//------------------------------------------------------------
bool OpenALSoundPlayer::primeStream(int64_t startFrame)
{
    if (sources.empty() || buffers.size() < sources.size() * 2) {
        return false;
    }
    std::unique_lock<std::mutex> lock(mutex);
    pendingSeekFrame.store(-1);
    if (!rebuildQueueLocked(startFrame, startFrame >= 0)) {
        return false;
    }
    streamPrimed = true;
    return true;
}

//------------------------------------------------------------
bool OpenALSoundPlayer::rebuildQueueLocked(int64_t startFrame, bool isSeek)
{
    spareBuffers.clear(); // every buffer is queued again below
    {
        std::lock_guard<std::mutex> hist(historyMutex);
        alSourceStopv(static_cast<ALsizei>(sources.size()), sources.data());
        alSourceRewindv(static_cast<ALsizei>(sources.size()), sources.data());
        for (ALuint source : sources) {
            alSourcei(source, AL_BUFFER, 0);
        }
        resetHistoryLocked();
    }
    pendingStartNs.store(0);
    xfPos = -1;
    renderEnded = false;
    endNotified = false;
    outCounter = 0;
    applyPendingRegion();

    const int64_t begin = regFromStart ? 0 : regS;
    int64_t start = startFrame < 0 ? begin : startFrame;
    if (!regFromStart) {
        start = std::max(start, regS);
    }
    if (stopFrame() < INT64_MAX / 8) {
        start = std::min(start, std::max<int64_t>(0, stopFrame() - 1));
    }
    start = std::max<int64_t>(0, start);

    // Fade in when starting mid-waveform: over the crossfade at the loop start, briefly after a seek.
    fadeInDone = 0;
    if (start <= 0) {
        fadeInLen = 0;
    } else if (!isSeek && start == regS) {
        fadeInLen = edgeFadeFrames;
    } else {
        fadeInLen = std::max<int64_t>(16, samplerate / 200);
    }
    seekDecoder(start);

    int queued = 0;
    for (int s = 0; s < 2 && !renderEnded; ++s) {
        ChunkRecord rec;
        if (renderChunk(rec) <= 0) {
            break;
        }
        std::lock_guard<std::mutex> hist(historyMutex);
        if (sources.size() == 1) {
            ALuint buffer = buffers[static_cast<size_t>(s)];
            if (fillBuffer(buffer, -1)) {
                alSourceQueueBuffers(sources[0], 1, &buffer);
            }
        } else {
            for (size_t i = 0; i < sources.size(); ++i) {
                ALuint buffer = buffers[static_cast<size_t>(s) * sources.size() + i];
                if (fillBuffer(buffer, static_cast<int>(i))) {
                    alSourceQueueBuffers(sources[i], 1, &buffer);
                }
            }
        }
        pushHistoryLocked(rec);
        ++queued;
    }
    stream_end = renderEnded;
    return queued > 0;
}

//------------------------------------------------------------
bool OpenALSoundPlayer::isLoaded() const{
	return bLoadedOk;
}

//------------------------------------------------------------
void OpenALSoundPlayer::threadedFunction()
{
    while (isThreadRunning()) {
        sleepMs(1);
        std::unique_lock<std::mutex> lock(mutex);
        // Pause and stop clear the running flag under this lock, so nothing below restarts them.
        if (!isThreadRunning() || sources.empty()) {
            break;
        }

        // A seek from the UI is applied here, by the thread that owns the decoder, so the
        // UI never waits on this lock. The queued audio is dropped and refilled from the new
        // spot, then restarted straight away.
        const int64_t seek = pendingSeekFrame.exchange(-1);
        if (seek >= 0) {
            const bool ok = rebuildQueueLocked(seek, true);
            if (ok && !bPaused) {
                alSourcePlayv(static_cast<ALsizei>(sources.size()), sources.data());
            }
            if (renderEnded) {
                stopThread();
                const bool notify = !endNotified;
                endNotified = true;
                lock.unlock();
                playerPtr = this;
                if (notify) {
                    notifyPlaybackEnded(this);
                }
                break;
            }
            continue;
        }

        ALint state = AL_STOPPED;
        ALint processed = 0;
        alGetSourcei(sources[0], AL_SOURCE_STATE, &state);
        alGetSourcei(sources[0], AL_BUFFERS_PROCESSED, &processed);
        while (processed > 0 && !renderEnded) {
            ChunkRecord rec;
            if (renderChunk(rec) <= 0) {
                break;
            }
            std::lock_guard<std::mutex> hist(historyMutex);
            for (size_t i = 0; i < sources.size(); ++i) {
                ALuint buffer = 0;
                alSourceUnqueueBuffers(sources[i], 1, &buffer);
                fillBuffer(buffer, sources.size() > 1 ? static_cast<int>(i) : -1);
                alSourceQueueBuffers(sources[i], 1, &buffer);
            }
            queuedRecords = std::max(0, queuedRecords - 1);
            pushHistoryLocked(rec);
            --processed;
        }

        if (renderEnded && processed > 0) {
            // Drop finished buffers so an underrun restart below can't replay them. They are
            // kept as spares in case playback carries on (see setPaused).
            std::lock_guard<std::mutex> hist(historyMutex);
            for (ALint k = 0; k < processed; ++k) {
                for (ALuint source : sources) {
                    ALuint buffer = 0;
                    alSourceUnqueueBuffers(source, 1, &buffer);
                    spareBuffers.push_back(buffer);
                }
                queuedRecords = std::max(0, queuedRecords - 1);
            }
        }

        // Underrun: the queue ran dry before it was refilled. Play what is queued now.
        if (state == AL_STOPPED) {
            ALint queuedNow = 0;
            alGetSourcei(sources[0], AL_BUFFERS_QUEUED, &queuedNow);
            if (queuedNow > 0) {
                alSourcePlayv(static_cast<ALsizei>(sources.size()), sources.data());
            }
        }

        if (renderEnded) {
            // The last chunk is queued: report the end now, as before, and let it play out.
            // Resuming a paused tail must not report it a second time.
            stopThread();
            const bool notify = !endNotified;
            endNotified = true;
            lock.unlock();
            playerPtr = this;
            if (notify) {
                notifyPlaybackEnded(this);
            }
            break;
        }
    }
}

//------------------------------------------------------------
void OpenALSoundPlayer::update(){
    if(sources.empty()) return;

    if(bLoadedOk && !streamPrimed && !isThreadRunning()) {
        ALint state = AL_STOPPED;
        alGetSourcei(sources[0], AL_SOURCE_STATE, &state);
        if(state == AL_STOPPED) {
            waitForThread();
            primeStream();
        }
    }

    if(bUseEffects && bUseFilter && !sources.empty())
    {
        applySends();
    }
}

void OpenALSoundPlayer::applySends()
{
    for (int bus = 0; bus < kSendCount; ++bus) {
        if (filters[bus] == 0 || !sendAvailable(bus)) {
            continue;
        }
        alFilterf(filters[bus], AL_LOWPASS_GAIN, sends[bus]);
        for (ALuint source : sources) {
            alSource3i(source, AL_AUXILIARY_SEND_FILTER, static_cast<ALint>(effectSlots[bus]), bus, static_cast<ALint>(filters[bus]));
        }
    }
}

//------------------------------------------------------------
void OpenALSoundPlayer::unload(){
	haltPlayback();
    waitForThread();

    if(bUseEffects)
    {
    }

	// Only lock the thread where necessary.
	{
		std::unique_lock<std::mutex> lock(mutex);

        // Delete sources before buffers
        if (sources.size() > 0) {
            alDeleteSources(sources.size(), &sources[0]);
        }
        if (buffers.size() > 0) {
            alDeleteBuffers(buffers.size(), &buffers[0]);
        }
        sources.clear();
        buffers.clear();
        if(bUseFilter) {
            for (ALuint& filter : filters) {
                if (filter != 0) {
                    alDeleteFilters(1, &filter);
                    filter = 0;
                }
            }
            bUseFilter = false;
        }
	}

	// Free resources and close file descriptors.
#ifdef FEEDRA_USING_MPG123
	if(mp3streamf){
		mpg123_close(mp3streamf);
		mpg123_delete(mp3streamf);
	}
	mp3streamf = 0;
#endif

	if(streamf){
		sf_close(streamf);
	}
	streamf = 0;
    file_extension = "";

    {
        std::lock_guard<std::mutex> hist(historyMutex);
        resetHistoryLocked();
    }
    totalFrames = 0;
    memoryBacked = false;
    std::vector<short>().swap(memShort);
    std::vector<float>().swap(memFloat);
    memFrames = 0;
    decPos.store(0);
    headCacheValid = false;
    renderEnded = false;
    xfPos = -1;
    // The requested loop region stays: it belongs to the sample, not to this load.
    appliedRegionVersion.store(0);

	bLoadedOk = false;
}

//------------------------------------------------------------
bool OpenALSoundPlayer::isPlaying() const{
	if(sources.empty()) return false;
	if(isStreaming) return isThreadRunning();
	ALint state;
	bool playing=false;
	for(int i=0;i<(int)sources.size();i++){
		alGetSourcei(sources[i],AL_SOURCE_STATE,&state);
		playing |= (state == AL_PLAYING);
	}
	return playing;
}

//------------------------------------------------------------
bool OpenALSoundPlayer::isPaused() const{
	if(sources.empty()) return false;
	ALint state;
	bool paused=true;
	for(int i=0;i<(int)sources.size();i++){
		alGetSourcei(sources[i],AL_SOURCE_STATE,&state);
		paused &= (state == AL_PAUSED);
	}
	return paused;
}

//------------------------------------------------------------
float OpenALSoundPlayer::getSpeed() const{
	return speed;
}

//------------------------------------------------------------
float OpenALSoundPlayer::getPan() const{
	return pan;
}

//------------------------------------------------------------
float OpenALSoundPlayer::getVolume() const{
	return volume;
}

//------------------------------------------------------------
void OpenALSoundPlayer::setVolume(float vol){
	volume = vol;
	if(sources.empty()) return;
    if(sources.size() == 1){
        alSourcef(sources[sources.size()-1], AL_MAX_GAIN, 6);
        alSourcef (sources[sources.size()-1], AL_GAIN, vol);
	}else{
		setPan(pan);
	}
}

//------------------------------------------------------------
void OpenALSoundPlayer::setPan(float p){
	p = std::clamp(p, -1.f, 1.f);
	pan = p;
	if(sources.empty()) return;
    if(!canPan()) {
        //Non-spatialised stereo plays through a single stereo source, so panning does nothing
        return;
    }

    if(channels==1){
        float pos[3] = {pan, 0, -sqrtf(1.0f - pan*pan)};
        alSourcefv(sources[sources.size()-1],AL_POSITION,pos);
	}else{
        // calculates left/right volumes from pan-value (constant panning law)
        // see: Curtis Roads: Computer Music Tutorial p 460
		// thanks to jasch
        float angle = p * 0.7853981633974483f; // in radians from -45. to +45.
        float cosAngle = cos(angle);
        float sinAngle = sin(angle);
        float leftVol  = (cosAngle - sinAngle) * 0.7071067811865475; // multiplied by sqrt(2)/2
        float rightVol = (cosAngle + sinAngle) * 0.7071067811865475; // multiplied by sqrt(2)/2
		for(int i=0;i<(int)channels;i++){
			if(i==0){
                alSourcef(sources[sources.size()-channels+i], AL_MAX_GAIN, 6);
                alSourcef(sources[sources.size()-channels+i], AL_GAIN,leftVol*volume);
			}else{
                alSourcef(sources[sources.size()-channels+i], AL_MAX_GAIN, 6);
                alSourcef(sources[sources.size()-channels+i], AL_GAIN,rightVol*volume);
			}
		}
	}
}


//------------------------------------------------------------
void OpenALSoundPlayer::setPosition(float pct){
    seekTo(pct);
}

//------------------------------------------------------------
void OpenALSoundPlayer::setPositionMS(int ms){
    if(sources.empty() || totalFrames <= 0 || samplerate <= 0) return;
    seekTo(static_cast<float>(double(ms) / 1000.0 * samplerate / double(totalFrames)));
}

//------------------------------------------------------------
float OpenALSoundPlayer::getPosition() const{
    if(sources.empty() || totalFrames <= 0) return 0;
    double frame = getAudibleFrame();
    if(frame < 0.0) frame = double(decPos.load());
    return static_cast<float>(std::clamp(frame / double(totalFrames), 0.0, 1.0));
}

//------------------------------------------------------------
int OpenALSoundPlayer::getPositionMS() const{
    if(sources.empty() || samplerate <= 0) return 0;
    double frame = getAudibleFrame();
    if(frame < 0.0) frame = double(decPos.load());
    return static_cast<int>(frame * 1000.0 / samplerate);
}

//------------------------------------------------------------
void OpenALSoundPlayer::setPaused(bool bP){
    if(sources.empty()) return;
    if(!bLoadedOk) return;
    ALint state = AL_STOPPED;
    alGetSourcei(sources[0], AL_SOURCE_STATE, &state);
    if(!bP) {
        if(state == AL_PLAYING && isThreadRunning()) {
            return; // already playing; playing it again would restart the queue
        }
        if(state == AL_PLAYING && !isThreadRunning()) {
            // Reached its end and is still playing the last of the queue: carry on from the
            // begin frame straight after it, as the old stream loop did.
            waitForThread();
            {
                std::unique_lock<std::mutex> lock(mutex);
                xfPos = -1;
                renderEnded = false;
                stream_end = false;
                endNotified = false;
                applyPendingRegion();
                const int64_t begin = regFromStart ? 0 : regS;
                seekDecoder(begin);
                fadeInDone = 0;
                fadeInLen = begin > 0 ? edgeFadeFrames : 0;
                bPaused = false;
                refillSparesLocked();
            }
            streamPrimed = false;
            startThread();
            return;
        }
        if(!streamPrimed && !isThreadRunning() && state != AL_PAUSED) {
            waitForThread();
            primeStream();
        }
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        bPaused = bP;
        if(bPaused){
            threadRunning = false;
            alSourcePausev(static_cast<ALsizei>(sources.size()), sources.data());
        }else{
            pendingStartNs.store(0);
            alSourcePlayv(static_cast<ALsizei>(sources.size()), sources.data());
        }
    }
    if(bPaused){
        stopThread();
        waitForThread();
    }else{
        streamPrimed = false;
        startThread();
    }
}

//------------------------------------------------------------
void OpenALSoundPlayer::setSpeed(float spd){
    if(bMultiPlay) {
        for(int i=0;i<channels;i++){
            alSourcef(sources[sources.size()-channels+i],AL_PITCH,spd);
        }
    } else {
        for(int i=0;i < sources.size();i++){
            alSourcef(sources[i],AL_PITCH,spd);
        }
    }
	speed = spd;
}


// ----------------------------------------------------------------------------
void OpenALSoundPlayer::play(){
    if(sources.empty()) return;
    if(!bLoadedOk) return;

    if(!streamPrimed) {
        waitForThread();
        primeStream();
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        bPaused = false;
        pendingStartNs.store(0);
        alSourcePlayv(static_cast<ALsizei>(sources.size()), sources.data());
    }
    streamPrimed = false;
    startThread();
}

// ----------------------------------------------------------------------------
void OpenALSoundPlayer::stop(){
    haltPlayback();
    if(bLoadedOk){
        waitForThread();
        primeStream();
    }
}

// ----------------------------------------------------------------------------
void OpenALSoundPlayer::haltPlayback(){
    if(sources.empty()) return;
    if(!bLoadedOk) return;
    {
        std::unique_lock<std::mutex> lock(mutex);
        // Cleared under the lock so the stream thread can't restart the sources after this.
        threadRunning = false;
        alSourceStopv(static_cast<ALsizei>(sources.size()), sources.data());
    }
    pendingSeekFrame.store(-1);
    pendingStartNs.store(0);
    stream_end = true;
}

// ----------------------------------------------------------------------------
void OpenALSoundPlayer::seekTo(float pct)
{
    if (!bLoadedOk || sources.empty() || totalFrames <= 0) {
        return;
    }
    pct = std::clamp(pct, 0.0f, 1.0f);
    int64_t frame = std::min<int64_t>(totalFrames - 1, static_cast<int64_t>(double(pct) * double(totalFrames)));

    // Keep inside the part of the file that plays: before E, and not in the intro when
    // "play from start" is off.
    int64_t S = 0, E = 0, xf = 0;
    const LoopRegion r = getLoopRegion();
    effectiveRegion(r, samplerate, totalFrames, S, E, xf);
    // Past E only into an outro: when not looping and playing to the end.
    if (r.loop || !r.playToEnd) {
        frame = std::min(frame, std::max<int64_t>(0, E - 1));
    }
    if (!r.playFromStart) {
        frame = std::max(frame, S);
    }

    if (isThreadRunning()) {
        // Latest request wins, so dragging just overwrites it.
        pendingSeekFrame.store(frame);
        return;
    }
    waitForThread();
    ALint state = AL_STOPPED;
    alGetSourcei(sources[0], AL_SOURCE_STATE, &state);
    if (state == AL_PLAYING) {
        // Still playing the last of the queue after its end was reached: carry on playing
        // from the new spot. Its "ended" notice is withdrawn (hasEnded() turns false).
        {
            std::unique_lock<std::mutex> lock(mutex);
            rebuildQueueLocked(frame, true);
            bPaused = false;
            pendingStartNs.store(0);
            alSourcePlayv(static_cast<ALsizei>(sources.size()), sources.data());
        }
        streamPrimed = false;
        // Also when the new spot is within the last chunks: the thread then reports the end.
        startThread();
        return;
    }
    // Stopped or paused: the stream thread is not running, so the queue can be rebuilt
    // here without contention. Leaves the sources stopped with the new audio queued.
    const bool wasPaused = bPaused;
    if (primeStream(frame)) {
        streamPrimed = true;
    }
    bPaused = wasPaused;
}

// ----------------------------------------------------------------------------
double OpenALSoundPlayer::getAudibleFrame() const
{
    if (sources.empty() || !bLoadedOk || samplerate <= 0) {
        return -1.0;
    }
    std::lock_guard<std::mutex> lock(historyMutex);
    if (historyCount == 0) {
        return -1.0;
    }
    ALint state = AL_STOPPED;
    alGetSourcei(sources[0], AL_SOURCE_STATE, &state);
    double frame = -1.0;
    if (queuedRecords == 0 || (state == AL_STOPPED && historyEnded)) {
        // Played out to the end.
        frame = double(lastFileFrame);
    } else {
        double offset = 0.0;
        double latencyFrames = 0.0;
        if (alGetSourcedvSOFT) {
            ALdouble values[2] = { 0.0, 0.0 };
            alGetSourcedvSOFT(sources[0], AL_SEC_OFFSET_LATENCY_SOFT, values);
            offset = values[0] * samplerate;
            latencyFrames = values[1] * samplerate * std::max(0.0f, speed);
        } else {
            ALint sampleOffset = 0;
            alGetSourcei(sources[0], AL_SAMPLE_OFFSET, &sampleOffset);
            offset = sampleOffset;
        }
        const ChunkRecord& head = history[(historyNewest - (queuedRecords - 1) + kHistory) % kHistory];
        double out = double(head.outStart) + offset;
        if (state == AL_PLAYING) {
            // Until the output delay has passed, nothing from this run is audible: hold at its
            // start rather than stepping back past it.
            out -= std::min(latencyFrames, out);
        }
        frame = mapOutputFrameLocked(out);
    }
    if (frame < 0.0) {
        return -1.0;
    }
    if (totalFrames > 0) {
        frame = std::min(frame, double(totalFrames));
    }
    return frame;
}

float OpenALSoundPlayer::getAudiblePosition() const
{
    const double frame = getAudibleFrame();
    if (frame < 0.0 || totalFrames <= 0) {
        return -1.0f;
    }
    return static_cast<float>(std::min(1.0, frame / double(totalFrames)));
}

// ----------------------------------------------------------------------------
void OpenALSoundPlayer::initFFT(int bands){
	if(int(bins.size())==bands) return;
	int signalSize = (bands-1)*2;
	if(fftCfg!=0) kiss_fftr_free(fftCfg);
	fftCfg = kiss_fftr_alloc(signalSize, 0, nullptr, nullptr);
	cx_out.resize(bands);
	bins.resize(bands);
	createWindow(signalSize);
}

// ----------------------------------------------------------------------------
void OpenALSoundPlayer::initSystemFFT(int bands){
	if(int(systemBins.size())==bands) return;
	int signalSize = (bands-1)*2;
	if(systemFftCfg!=0) kiss_fftr_free(systemFftCfg);
	systemFftCfg = kiss_fftr_alloc(signalSize, 0, nullptr, nullptr);
	systemCx_out.resize(bands);
	systemBins.resize(bands);
	createWindow(signalSize);
}

float * OpenALSoundPlayer::getCurrentBufferSum(int size){
	if(int(windowedSignal.size())!=size){
		windowedSignal.resize(size);
	}
	windowedSignal.assign(windowedSignal.size(),0);
	for(int k=0;k<int(sources.size())/channels;k++){
		if(!isStreaming){
			ALint state;
			alGetSourcei(sources[k*channels],AL_SOURCE_STATE,&state);
			if( state != AL_PLAYING ) continue;
		}
		int pos;
		alGetSourcei(sources[k*channels],AL_SAMPLE_OFFSET,&pos);
		//if(pos+size>=(int)fftBuffers[0].size()) continue;
		for(int i=0;i<channels;i++){
			float gain;
			alGetSourcef(sources[k*channels+i],AL_GAIN,&gain);
			for(int j=0;j<size;j++){
				if(pos+j<(int)fftBuffers[i].size())
					windowedSignal[j]+=fftBuffers[i][pos+j]*gain;
				else
					windowedSignal[j]=0;
			}
		}
	}
	return &windowedSignal[0];
}

// ----------------------------------------------------------------------------
float * OpenALSoundPlayer::getSpectrum(int bands){
	initFFT(bands);
	bins.assign(bins.size(),0);
	if(sources.empty()) return &bins[0];

	int signalSize = (bands-1)*2;
	getCurrentBufferSum(signalSize);

	float normalizer = 2. / windowSum;
	runWindow(windowedSignal);
	kiss_fftr(fftCfg, &windowedSignal[0], &cx_out[0]);
	for(int i= 0; i < bands; i++) {
		bins[i] += sqrtf(cx_out[i].r * cx_out[i].r + cx_out[i].i * cx_out[i].i) * normalizer;
	}
	return &bins[0];
}

// ----------------------------------------------------------------------------
float * OpenALSoundPlayer::getSystemSpectrum(int bands){
	initSystemFFT(bands);
	systemBins.assign(systemBins.size(),0);
	if(players().empty()) return &systemBins[0];

	int signalSize = (bands-1)*2;
	if(int(systemWindowedSignal.size())!=signalSize){
		systemWindowedSignal.resize(signalSize);
	}
	systemWindowedSignal.assign(systemWindowedSignal.size(),0);

	set<OpenALSoundPlayer*>::iterator it;
	for(it=players().begin();it!=players().end();it++){
		if(!(*it)->isPlaying()) continue;
		float * buffer = (*it)->getCurrentBufferSum(signalSize);
		for(int i=0;i<signalSize;i++){
			systemWindowedSignal[i]+=buffer[i];
		}
	}

	float normalizer = 2. / windowSum;
	runWindow(systemWindowedSignal);
	kiss_fftr(systemFftCfg, &systemWindowedSignal[0], &systemCx_out[0]);
	for(int i= 0; i < bands; i++) {
		systemBins[i] += sqrtf(systemCx_out[i].r * systemCx_out[i].r + systemCx_out[i].i * systemCx_out[i].i) * normalizer;
	}
	return &systemBins[0];
}

// ----------------------------------------------------------------------------
void OpenALSoundPlayer::runWindow(vector<float> & signal){
	for(int i = 0; i < (int)signal.size(); i++)
		signal[i] *= window[i];
}
