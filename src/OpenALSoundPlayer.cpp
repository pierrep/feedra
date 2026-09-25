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

static bool bUseEffects = false;
static bool bUseConvolution = false;
static int g_slotCount = 0;
static ALuint irBuffer = 0;
static float g_convolutionGain = 1.0f / 16.0f;
static std::filesystem::path g_irPath;
static ALuint effects[3] = { 0, 0, 0 };
static ALuint effectSlots[3] = { 0, 0, 0 };
static EFXEAXREVERBPROPERTIES reverbs[2] = {
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

static bool applyConvolutionSlot()
{
    if (!bUseConvolution || effectSlots[2] == 0 || effects[2] == 0 || irBuffer == 0) {
        return false;
    }
    alGetError();
    alAuxiliaryEffectSloti(effectSlots[2], AL_BUFFER, static_cast<ALint>(irBuffer));
    alAuxiliaryEffectSlotf(effectSlots[2], AL_EFFECTSLOT_GAIN, g_convolutionGain);
    alAuxiliaryEffectSloti(effectSlots[2], AL_EFFECTSLOT_EFFECT, static_cast<ALint>(effects[2]));
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
    alEffectf(effect, AL_EAXREVERB_GAIN, reverb->flGain);
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

int g_reverbIndex = -1;

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

void applyReverbPreset(int index)
{
    reverbs[0] = kReverbPresets[index].props;
    reverbs[1] = kReverbPresets[index].props;
    if (!bUseEffects || effects[0] == 0 || effects[1] == 0) {
        return;
    }
    if (!LoadEffect(effects[0], &reverbs[0]) || !LoadEffect(effects[1], &reverbs[1])) {
        return;
    }
    alAuxiliaryEffectSloti(effectSlots[0], AL_EFFECTSLOT_EFFECT, static_cast<ALint>(effects[0]));
    alAuxiliaryEffectSloti(effectSlots[1], AL_EFFECTSLOT_EFFECT, static_cast<ALint>(effects[1]));
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

int OpenALSoundPlayer::reverbPresetIndex()
{
    if (g_reverbIndex < 0) {
        g_reverbIndex = alleyPresetIndex();
    }
    return g_reverbIndex;
}

void OpenALSoundPlayer::setReverbPreset(int index)
{
    if (index < 0 || index >= reverbPresetCount()) {
        return;
    }
    g_reverbIndex = index;
    applyReverbPreset(index);
}

bool OpenALSoundPlayer::setReverbPresetById(const std::string& id)
{
    for (int i = 0; i < reverbPresetCount(); ++i) {
        if (id == kReverbPresets[i].id) {
            setReverbPreset(i);
            return true;
        }
    }
    return false;
}

float OpenALSoundPlayer::defaultConvolutionGain()
{
    return 1.0f / 16.0f;
}

float OpenALSoundPlayer::convolutionGain()
{
    return g_convolutionGain;
}

void OpenALSoundPlayer::setConvolutionGain(float gain)
{
    if (gain < 0.0f) {
        gain = 0.0f;
    } else if (gain > 1.0f) {
        gain = 1.0f;
    }
    g_convolutionGain = gain;
    if (bUseConvolution && effectSlots[2] != 0 && irBuffer != 0) {
        alAuxiliaryEffectSlotf(effectSlots[2], AL_EFFECTSLOT_GAIN, g_convolutionGain);
        alGetError();
    }
}

bool OpenALSoundPlayer::convolutionAvailable()
{
    return bUseConvolution;
}

std::filesystem::path OpenALSoundPlayer::convolutionImpulsePath()
{
    return g_irPath;
}

bool OpenALSoundPlayer::setConvolutionImpulse(const std::filesystem::path& path)
{
    if (!bUseConvolution || path.empty()) {
        return false;
    }
    const ALuint buffer = loadImpulseBuffer(path);
    if (!buffer) {
        return false;
    }
    const ALuint previous = irBuffer;
    irBuffer = buffer;
    if (!applyConvolutionSlot()) {
        irBuffer = previous;
        alDeleteBuffers(1, &buffer);
        return false;
    }
    g_irPath = path;
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
    reverbSend      = 0.0f;
    reverbSend2     = 0.0f;
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

                /* Generate FX slots. The third effect is the convolution reverb. */
                alGenEffects(3, effects);
                if(!LoadEffect(effects[0], &reverbs[0]) || !LoadEffect(effects[1], &reverbs[1]))
                {
                    qCritical( ) <<  "Failed to load effects, aborting...";
                    bUseEffects = false;
                    alDeleteEffects(3, effects);
                    effects[0] = effects[1] = effects[2] = 0;
                    close();
                    return;
                }

                bUseConvolution = createConvolutionEffect(effects[2]) != 0;
                if (!bUseConvolution) {
                    alDeleteEffects(1, &effects[2]);
                    effects[2] = 0;
                }

                g_slotCount = bUseConvolution ? 3 : 2;
                alGenAuxiliaryEffectSlots(g_slotCount, effectSlots);

                /* Tell the effect slots to use the loaded effect objects, with slot 0 for
                 * Zone 0 and slot 1 for Zone 1. Note that this effectively copies the
                 * effect properties. Modifying or deleting the effect object afterward
                 * won't directly affect the effect slot until they're reapplied like this.
                 * Slot 2 receives the impulse response when one is loaded.
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
                if (irBuffer != 0) {
                    alDeleteBuffers(1, &irBuffer);
                    irBuffer = 0;
                }
                g_irPath.clear();
                bUseEffects = false;
                bUseConvolution = false;
                g_slotCount = 0;
            }

			alcMakeContextCurrent(nullptr);
			alcDestroyContext(alContext);
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
//------------------------------------------------------------
bool OpenALSoundPlayer::mpg123ReadFile(const std::filesystem::path& path){
	int err = MPG123_OK;
	mpg123_handle * f = mpg123_new(nullptr,&err);
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
    while(mpg123_read(f,(unsigned char*)&buffer_short[buffer_short.size()-buffer_size/2],buffer_size,&done)!=MPG123_DONE){
        buffer_short.resize(buffer_short.size()+buffer_size/2);
	};
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
bool OpenALSoundPlayer::sfStream(const std::filesystem::path& path){
	if(!streamf){
		SF_INFO sfInfo;
		streamf = sf_open(path.string().c_str(),SFM_READ,&sfInfo);
		if(!streamf){
            qCritical() << "OpenALSoundPlayer" << "sfStream(): couldn't read " << path.string().c_str();
			return false;
		}

        int stream_subformat = sfInfo.format & SF_FORMAT_SUBMASK ;
		if (stream_subformat == SF_FORMAT_FLOAT || stream_subformat == SF_FORMAT_DOUBLE){
			sf_command (streamf, SFC_CALC_SIGNAL_MAX, &stream_scale, sizeof (stream_scale)) ;
			if (stream_scale < 1e-10)
				stream_scale = 1.0 ;
			else
				stream_scale = 32700.0 / stream_scale ;
		}

		channels = sfInfo.channels;
		duration = float(sfInfo.frames) / float(sfInfo.samplerate);
		samplerate = sfInfo.samplerate;
		stream_samples_read = 0;
	}

	int curr_buffer_size = BUFFER_STREAM_SIZE*channels;
	if(speed>1) curr_buffer_size *= (int)round(speed);
    buffer_short.resize(curr_buffer_size);
    buffer_float.resize(buffer_short.size());
    if (sample_format == FormatType::Float){
        sf_count_t samples_read = sf_read_float (streamf, &buffer_float[0], buffer_float.size());
        //cout << "float stream .... samples_read = " << samples_read << endl;
		stream_samples_read += samples_read;
        if(samples_read<(int)buffer_float.size()){
            buffer_float.resize(samples_read);
            buffer_short.resize(samples_read);

            // set to start of stream
            sf_seek(streamf,0,SEEK_SET);

            if(!bLoop) {
                stopThread();
            }
			stream_samples_read = 0;
            //cout << "End of float stream, stream_samples_read = 0" << endl;
			stream_end = true;
		}
        for (int i = 0 ; i < int(buffer_float.size()) ; i++){
            //buffer_float[i] *= stream_scale ;
            buffer_short[i] = 32565.0 * buffer_float[i] * stream_scale;
		}
	}else{
        sf_count_t frames_read = sf_readf_short(streamf,&buffer_short[0],curr_buffer_size/channels);
		stream_samples_read += frames_read*channels;
        //cout << "sfStream()   frames_read = " << frames_read << " stream_samples_read: " << stream_samples_read << endl;
        if(frames_read < curr_buffer_size/channels){
            buffer_float.resize(frames_read*channels);
            buffer_short.resize(frames_read*channels);

            // set to start of stream
            sf_seek(streamf,0,SEEK_SET);

            if(!bLoop) {
                stopThread();
            }
			stream_samples_read = 0;
            //cout << "End of short stream, stream_samples_read = 0" << endl;
			stream_end = true;
		}
        for(int i=0;i<(int)buffer_short.size();i++){
            buffer_float[i]=float(buffer_short[i])/32565.0f;
		}
	}

	return true;
}

#ifdef FEEDRA_USING_MPG123
//------------------------------------------------------------
bool OpenALSoundPlayer::mpg123Stream(const std::filesystem::path& path){
	if(!mp3streamf){
		int err = MPG123_OK;
		mp3streamf = mpg123_new(nullptr,&err);
		if(mpg123_open(mp3streamf,path.string().c_str())!=MPG123_OK){
			mpg123_close(mp3streamf);
			mpg123_delete(mp3streamf);
            mp3streamf = 0;
            qCritical() << "OpenALSoundPlayer" << "mpg123Stream(): couldn't read " << path.string().c_str();
			return false;
		}

		long int rate;
		mpg123_getformat(mp3streamf,&rate,&channels,(int*)&stream_encoding);
        subformat_string = getMpg123EncodingString(stream_encoding);
		if(stream_encoding!=MPG123_ENC_SIGNED_16){
			qCritical() << "OpenALSoundPlayer" << "mpg123Stream(): " << getMpg123EncodingString(stream_encoding).c_str()
			<< " encoding for \"" << path.string().c_str() << "\"" << " unsupported, expecting MPG123_ENC_SIGNED_16";
			return false;
		}
		samplerate = rate;
		mp3_buffer_size = mpg123_outblock( mp3streamf );


		mpg123_seek(mp3streamf,0,SEEK_END);
		off_t samples = mpg123_tell(mp3streamf);
        duration = float(samples) / float(samplerate);
		mpg123_seek(mp3streamf,0,SEEK_SET);
	}

	int curr_buffer_size = mp3_buffer_size;
	if(speed>1) curr_buffer_size *= (int)round(speed);
    buffer_short.resize(curr_buffer_size);
    buffer_float.resize(buffer_short.size());
	size_t done=0;
    if(mpg123_read(mp3streamf,(unsigned char*)&buffer_short[0],curr_buffer_size*2,&done)==MPG123_DONE){
        //set to start of stream
        mpg123_seek(mp3streamf,0,SEEK_SET);

        buffer_short.resize(done/2);
        buffer_float.resize(done/2);
		if(!bLoop) stopThread();
		stream_end = true;
	}


    for(int i=0;i<(int)buffer_short.size();i++){
        buffer_float[i] = float(buffer_short[i])/32565.f;
	}

	return true;
}
#endif

//------------------------------------------------------------
size_t OpenALSoundPlayer::stream(const std::filesystem::path& fileName){
#ifdef FEEDRA_USING_MPG123
    if(file_extension == ".mp3" || mp3streamf){
        if(!mpg123Stream(fileName)) return 0;
	}else
#endif
        if(!sfStream(fileName)) return 0;

	fftBuffers.resize(channels);
    int numFrames = (int) buffer_float.size()/channels;

	for(int i=0;i<channels;i++){
		fftBuffers[i].resize(numFrames);
		for(int j=0;j<numFrames;j++){
            fftBuffers[i][j] = buffer_float[j*channels+i];
		}
	}
    return numFrames;
}

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
void OpenALSoundPlayer::setSpatialisedStereo(bool val)
{
    if(bLoadedOk)
    {
        if(channels == 2)
        {
            if(spatialisedStereo != val)
            {
                setPaused(true);
                bLoadedOk = false;
                spatialisedStereo = val;
                load(fileName,isStreaming);
            }
        }
    }
}

//------------------------------------------------------------
namespace {

struct DecodeStream {
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

    bool open(const std::filesystem::path& path, const std::string& ext, bool allowFloat)
    {
        if (ext == ".mp3") {
#ifndef FEEDRA_USING_MPG123
            qCritical() << "OpenALSoundPlayer" << "decodeFile(): mp3 support is not built";
            return false;
#else
            mp3 = true;
            fileFormat = 0x230000;
            int err = MPG123_OK;
            mpg = mpg123_new(nullptr, &err);
            if (!mpg || mpg123_open(mpg, path.string().c_str()) != MPG123_OK) {
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
                            << "encoding for" << path.string().c_str() << "unsupported";
                close();
                return false;
            }
            sampleRate = static_cast<int>(rate);
            mp3BufferSize = mpg123_outblock(mpg);
            mpg123_seek(mpg, 0, SEEK_END);
            const off_t samples = mpg123_tell(mpg);
            duration = sampleRate > 0 ? float(samples) / float(sampleRate) : 0.0f;
            mpg123_seek(mpg, 0, SEEK_SET);
            return channels > 0 && sampleRate > 0;
#endif
        }

        memset(&info, 0, sizeof(info));
        snd = sf_open(path.string().c_str(), SFM_READ, &info);
        if (!snd) {
            return false;
        }
        fileFormat = info.format;
        channels = info.channels;
        sampleRate = info.samplerate;
        duration = info.samplerate > 0 ? float(info.frames) / float(info.samplerate) : 0.0f;
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
            if (code == MPG123_DONE) {
                mpg123_seek(mpg, 0, SEEK_SET);
                shorts.resize(done / 2);
                floats.resize(done / 2);
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
            while (mpg123_read(mpg, reinterpret_cast<unsigned char*>(&shorts[shorts.size() - bufferSize / 2]), bufferSize, &done) != MPG123_DONE) {
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
    out.formatString = getSoundFileFormatString(out.fileFormat);
    out.subformatString = stream.mp3 ? stream.subformat : getSoundFileSubFormatString(out.fileFormat);

    if (!isStream) {
        if (!stream.readAll(out.pcmShort, out.pcmFloat)) {
            qCritical() << "Sound file load failed - wrong file type or empty file";
            return out;
        }
        out.duration = stream.duration;
        out.ok = true;
        return out;
    }

    std::vector<short> discardedShort;
    std::vector<float> discardedFloat;
    if (!stream.readChunk(discardedShort, discardedFloat)) {
        qCritical() << "Sound file load failed - wrong file type or empty file";
        return out;
    }
    out.initialChunks.resize(2);
    for (DecodedChunk& chunk : out.initialChunks) {
        stream.readChunk(chunk.pcmShort, chunk.pcmFloat);
    }
    out.streamEnded = stream.ended;
    out.streamSamplesRead = static_cast<int64_t>(stream.samplesRead);
    out.resumeFrames = stream.framePosition();
    out.duration = stream.duration;
    out.ok = true;
    return out;
}

bool OpenALSoundPlayer::attachDecodedStream(const DecodedAudio& decoded)
{
#ifdef FEEDRA_USING_MPG123
    if (decoded.mp3) {
        int err = MPG123_OK;
        mp3streamf = mpg123_new(nullptr, &err);
        if (!mp3streamf || mpg123_open(mp3streamf, decoded.path.string().c_str()) != MPG123_OK) {
            qCritical() << "OpenALSoundPlayer" << "attachDecodedStream(): couldn't read" << decoded.path.string().c_str();
            if (mp3streamf) {
                mpg123_close(mp3streamf);
                mpg123_delete(mp3streamf);
                mp3streamf = nullptr;
            }
            return false;
        }
        mpg123_seek(mp3streamf, static_cast<off_t>(decoded.resumeFrames), SEEK_SET);
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
    if (decoded.resumeFrames > 0) {
        sf_seek(streamf, static_cast<sf_count_t>(decoded.resumeFrames), SEEK_SET);
    }
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
    if (bLoadedOk || streamf
#ifdef FEEDRA_USING_MPG123
        || mp3streamf
#endif
    ) {
        unload();
    }
    bLoadedOk = false;

    fileName = decoded.path;
    bMultiPlay = false;
    isStreaming = decoded.streaming;
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

    if (channels <= 0 || samplerate <= 0) {
        qCritical() << "Sound file load failed - wrong file type or empty file";
        return false;
    }

    if (isStreaming) {
        if (!attachDecodedStream(decoded)) {
            return false;
        }
        if (!decoded.initialChunks.empty()) {
            buffer_short = decoded.initialChunks.back().pcmShort;
            buffer_float = decoded.initialChunks.back().pcmFloat;
        }
    } else {
        buffer_short = std::move(decoded.pcmShort);
        buffer_float = std::move(decoded.pcmFloat);
        if (buffer_short.empty()) {
            qCritical() << "Sound file load failed - wrong file type or empty file";
            return false;
        }
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

    if (spatialisedStereo) {
        sources.resize(static_cast<size_t>(channels));
    } else {
        sources.resize(1);
    }

    alGetError();
    alGenSources(static_cast<ALsizei>(sources.size()), &sources[0]);
    ALenum err = alGetError();
    if (err != AL_NO_ERROR) {
        qCritical() << "OpenALSoundPlayer" << "loadSound(): couldn't generate sources for " << fileName.string().c_str() << ":"
                    << static_cast<int>(err) << getALErrorString(err).c_str();
        sources.clear();
        return false;
    }

    if (isStreaming) {
        buffers.resize(sources.size() * 2);
    } else {
        buffers.resize(sources.size());
    }
    alGenBuffers(static_cast<ALsizei>(buffers.size()), &buffers[0]);

    if (sources.size() == 1) {
        const int count = static_cast<int>(buffers.size());
        for (int i = 0; i < count; ++i) {
            const std::vector<short>* shorts = &buffer_short;
            const std::vector<float>* floats = &buffer_float;
            if (isStreaming) {
                if (i >= static_cast<int>(decoded.initialChunks.size())) {
                    break;
                }
                shorts = &decoded.initialChunks[static_cast<size_t>(i)].pcmShort;
                floats = &decoded.initialChunks[static_cast<size_t>(i)].pcmFloat;
            }
            if (!uploadPcm(buffers[static_cast<size_t>(i)], openALformat, samplerate, *shorts, *floats)) {
                qCritical() << "OpenALSoundPlayer:" << "loadSound(): couldn't create buffer for " << fileName.string().c_str();
                return false;
            }
        }
        if (isStreaming) {
            alSourceQueueBuffers(sources[0], static_cast<ALsizei>(buffers.size()), &buffers[0]);
        } else {
            alSourcei(sources[0], AL_BUFFER, buffers[0]);
            err = alGetError();
            if (err != AL_NO_ERROR) {
                qCritical() << "OpenALSoundPlayer:" << "loadSound(): couldn't source for" << fileName.string().c_str()
                            << static_cast<int>(err) << getALErrorString(err).c_str();
                return false;
            }
        }
        alSourcef(sources[0], AL_PITCH, 1.0f);
        alSourcef(sources[0], AL_GAIN, 1.0f);
        alSourcef(sources[0], AL_ROLLOFF_FACTOR, 0.0f);
        alSourcei(sources[0], AL_SOURCE_RELATIVE, AL_TRUE);
    } else {
        if (isStreaming) {
            for (int s = 0; s < 2; ++s) {
                if (s >= static_cast<int>(decoded.initialChunks.size())) {
                    break;
                }
                const DecodedChunk& chunk = decoded.initialChunks[static_cast<size_t>(s)];
                const int frames = channels > 0 ? static_cast<int>(chunk.pcmShort.size()) / channels : 0;
                for (int i = 0; i < channels; ++i) {
                    std::vector<short> channelShort(static_cast<size_t>(frames));
                    std::vector<float> channelFloat(static_cast<size_t>(frames));
                    for (int j = 0; j < frames; ++j) {
                        const size_t src = static_cast<size_t>(j * channels + i);
                        if (openALformat == AL_FORMAT_MONO16 && src < chunk.pcmShort.size()) {
                            channelShort[static_cast<size_t>(j)] = chunk.pcmShort[src];
                        } else if (openALformat == AL_FORMAT_MONO_FLOAT32 && src < chunk.pcmFloat.size()) {
                            channelFloat[static_cast<size_t>(j)] = chunk.pcmFloat[src];
                        }
                    }
                    const size_t bufferIndex = static_cast<size_t>(s * channels + i);
                    if (bufferIndex >= buffers.size()) {
                        qCritical() << "OpenALSoundPlayer" << "loadSound(): stereo buffer index out of range";
                        return false;
                    }
                    if (!uploadPcm(buffers[bufferIndex], openALformat, samplerate, channelShort, channelFloat)) {
                        qCritical() << "OpenALSoundPlayer" << "loadSound(): couldn't create stereo buffers for" << fileName.string().c_str();
                        return false;
                    }
                    alSourceQueueBuffers(sources[static_cast<size_t>(i)], 1, &buffers[bufferIndex]);
                }
            }
        } else {
            const int frames = static_cast<int>(buffer_short.size()) / channels;
            for (int i = 0; i < channels; ++i) {
                std::vector<short> channelShort(static_cast<size_t>(frames));
                std::vector<float> channelFloat(static_cast<size_t>(frames));
                for (int j = 0; j < frames; ++j) {
                    const size_t src = static_cast<size_t>(j * channels + i);
                    if (openALformat == AL_FORMAT_MONO16 && src < buffer_short.size()) {
                        channelShort[static_cast<size_t>(j)] = buffer_short[src];
                    } else if (openALformat == AL_FORMAT_MONO_FLOAT32 && src < buffer_float.size()) {
                        channelFloat[static_cast<size_t>(j)] = buffer_float[src];
                    }
                }
                if (!uploadPcm(buffers[static_cast<size_t>(i)], openALformat, samplerate, channelShort, channelFloat)) {
                    qCritical() << "OpenALSoundPlayer" << "loadSound(): couldn't create stereo buffers for" << fileName.string().c_str();
                    return false;
                }
                alSourcei(sources[static_cast<size_t>(i)], AL_BUFFER, buffers[static_cast<size_t>(i)]);
            }
        }

        for (int i = 0; i < channels; ++i) {
            err = alGetError();
            if (err != AL_NO_ERROR) {
                qCritical() << "OpenALSoundPlayer" << "loadSound(): couldn't create stereo sources for" << fileName.string().c_str()
                            << static_cast<int>(err) << getALErrorString(err).c_str();
                return false;
            }
            const float pos[3] = { i == 0 ? -1.0f : 1.0f, 0.0f, 0.0f };
            alSourcefv(sources[static_cast<size_t>(i)], AL_POSITION, pos);
            alSourcef(sources[static_cast<size_t>(i)], AL_ROLLOFF_FACTOR, 0.0f);
            alSourcei(sources[static_cast<size_t>(i)], AL_SOURCE_RELATIVE, AL_TRUE);
        }
    }

    if (bUseEffects && !sources.empty()) {
        reverbSend = 0.0f;
        reverbSend2 = 0.0f;
        alGenFilters(1, &filters[0]);
        alFilteri(filters[0], AL_FILTER_TYPE, AL_FILTER_LOWPASS);
        alFilterf(filters[0], AL_LOWPASS_GAIN, reverbSend);
        alSource3i(sources[0], AL_AUXILIARY_SEND_FILTER, static_cast<ALint>(effectSlots[0]), 0, filters[0]);
        if (bUseConvolution && effectSlots[2] != 0) {
            alGenFilters(1, &filters[1]);
            alFilteri(filters[1], AL_FILTER_TYPE, AL_FILTER_LOWPASS);
            alFilterf(filters[1], AL_LOWPASS_GAIN, reverbSend2);
            alSource3i(sources[0], AL_AUXILIARY_SEND_FILTER, static_cast<ALint>(effectSlots[2]), 1, filters[1]);
        }
        err = alGetError();
        if (err != AL_NO_ERROR) {
            qCritical() << "OpenALSoundPlayer:" << "attaching FX sends failed..."
                        << static_cast<int>(err) << getALErrorString(err).c_str();
            return false;
        }
        bUseFilter = true;
    }

    bLoadedOk = true;
    return bLoadedOk;
}

//------------------------------------------------------------
bool OpenALSoundPlayer::isLoaded() const{
	return bLoadedOk;
}

//------------------------------------------------------------
void OpenALSoundPlayer::threadedFunction(){
    vector<vector<short> > multibuffer_short;
    vector<vector<float> > multibuffer_float;

    if(openALformat == AL_FORMAT_MONO16) {
        multibuffer_short.resize(channels);
    } else if(openALformat == AL_FORMAT_MONO_FLOAT32) {
        multibuffer_float.resize(channels);
    }

	while(isThreadRunning()){
        sleepMs(1);
		std::unique_lock<std::mutex> lock(mutex);

        int loop;
        if(bMultiPlay) {
            loop = int(sources.size())/channels;
        } else {
            loop = 1;
        }
        for(int i=0; i < loop; i++){
            ALint state;
            int index;
            if(bMultiPlay) {
                index = i*channels;
            } else {
                index = 0;
            }
            if(sources.size()) {
                alGetSourcei(sources[index],AL_SOURCE_STATE,&state);
            }

            int processed = 0;
            if(sources.size()) {
                alGetSourcei(sources[index], AL_BUFFERS_PROCESSED, &processed);
            }
            while(processed)
			{
                processed--;
                stream("");

                if((channels > 1) && spatialisedStereo){
					for(int j=0;j<channels;j++){
                        if(openALformat == AL_FORMAT_MONO16) {
                            int numFrames = buffer_short.size()/channels;
                            multibuffer_short[j].resize(buffer_short.size()/channels);
                            for(int k=0;k<numFrames;k++){
                                multibuffer_short[j][k] = buffer_short[k*channels+j];
                            }
                        } else if(openALformat == AL_FORMAT_MONO_FLOAT32) {
                            int numFrames = buffer_float.size()/channels;
                            multibuffer_float[j].resize(buffer_float.size()/channels);
                            for(int k=0;k<numFrames;k++){
                                multibuffer_float[j][k] = buffer_float[k*channels+j];
                            }
                        }
						ALuint albuffer;
                        alSourceUnqueueBuffers(sources[i*channels+j], 1, &albuffer);
                        if(openALformat == AL_FORMAT_MONO16) {
                            alBufferData(albuffer,openALformat,&multibuffer_short[j][0],buffer_short.size()*2/channels,samplerate);
                        } else if(openALformat == AL_FORMAT_MONO_FLOAT32) {
                            alBufferData(albuffer,openALformat,&multibuffer_float[j][0],buffer_float.size()*4/channels,samplerate);
                        }
                        alSourceQueueBuffers(sources[i*channels+j], 1, &albuffer);
					}
				}else{
					ALuint albuffer;
					alSourceUnqueueBuffers(sources[i], 1, &albuffer);
                    if((openALformat == AL_FORMAT_MONO16) || (openALformat == AL_FORMAT_STEREO16)) {
                        alBufferData(albuffer,openALformat,&buffer_short[0],buffer_short.size()*2,samplerate);
                    } else if((openALformat == AL_FORMAT_MONO_FLOAT32) || (openALformat == AL_FORMAT_STEREO_FLOAT32)) {
                        alBufferData(albuffer,openALformat,&buffer_float[0],buffer_float.size()*4,samplerate);
                    }
					alSourceQueueBuffers(sources[i], 1, &albuffer);
				}
                if(stream_end && !(state == AL_STOPPED)){
                    //cout << "threadedFunction() - stream end! state: " << state << endl;
                    playerPtr = this;
                    notifyPlaybackEnded(playerPtr);
					break;
				}
			}

			bool stream_running=false;
			#ifdef FEEDRA_USING_MPG123
				stream_running = streamf || mp3streamf;
			#else
				stream_running = streamf;
			#endif                
            if(isThreadRunning()){
                if (state != AL_PLAYING && state != AL_PAUSED && stream_running && !stream_end) {
                    alSourcePlayv(sources.size(), &sources[0]);
                    //cout << "Loop stream!" << endl;
                    stream_end = false;
                }
			}

        }
	}
}

//------------------------------------------------------------
void OpenALSoundPlayer::update(){
    if(sources.empty()) return;

    if(bMultiPlay) {
        for(int i=1; i<int(sources.size())/channels; ){
            ALint state;
            alGetSourcei(sources[i*channels],AL_SOURCE_STATE,&state);

            ALdouble offsets[2];
            alGetSourcedvSOFT(sources[i*channels], AL_SEC_OFFSET_LATENCY_SOFT, offsets);
            qDebug() << " Offset: " << offsets[0] << " - Latency: " << (ALuint)(offsets[1]*1000) << " ms";
            if(state != AL_PLAYING){
                alDeleteSources(channels,&sources[i*channels]);
                for(int j=0;j<channels;j++){
                    sources.erase(sources.begin()+i*channels);
                }
            }else{
                i++;
            }
        }
    }

    if(bUseEffects && bUseFilter && !sources.empty())
    {
        alFilterf(filters[0], AL_LOWPASS_GAIN, reverbSend);
        alSource3i(sources[0], AL_AUXILIARY_SEND_FILTER, static_cast<ALint>(effectSlots[0]), 0, filters[0]);
        if (filters[1] != 0 && bUseConvolution && effectSlots[2] != 0) {
            alFilterf(filters[1], AL_LOWPASS_GAIN, reverbSend2);
            alSource3i(sources[0], AL_AUXILIARY_SEND_FILTER, static_cast<ALint>(effectSlots[2]), 1, filters[1]);
        }
    }
}

//------------------------------------------------------------
void OpenALSoundPlayer::unload(){
	stop();
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
            if (filters[0] != 0) {
                alDeleteFilters(1, &filters[0]);
                filters[0] = 0;
            }
            if (filters[1] != 0) {
                alDeleteFilters(1, &filters[1]);
                filters[1] = 0;
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
bool OpenALSoundPlayer::isLooping() const
{
    return bLoop;
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
void OpenALSoundPlayer::setPosition(float pct){
	setPositionMS(duration*pct*1000.f);
}

//------------------------------------------------------------
void OpenALSoundPlayer::setPositionMS(int ms){
	if(sources.empty()) return;
    std::unique_lock<std::mutex> lock(mutex);

#ifdef FEEDRA_USING_MPG123
	if(mp3streamf){
		mpg123_seek(mp3streamf,float(ms)/1000.f*samplerate,SEEK_SET);
//        int queued = 0;
//        alGetSourcei(sources[0], AL_BUFFERS_QUEUED, &queued);
//        int processed = 0;
//        alGetSourcei(sources[0], AL_BUFFERS_PROCESSED, &processed);
//        cout << "Buffers queued (setPositionMS) on source 0: " << queued << "  Buffers processed:" << processed << endl;
	}else
#endif
	if(streamf){
        stream_samples_read = sf_seek(streamf,float(ms)/1000.f*samplerate,SEEK_SET) * channels;
        //cout << "setPositionMS -> seek to " << ms << " stream_samples_read: " << stream_samples_read << endl;
//        int queued = 0;
//        alGetSourcei(sources[0], AL_BUFFERS_QUEUED, &queued);
//        int processed = 0;
//        alGetSourcei(sources[0], AL_BUFFERS_PROCESSED, &processed);
//        cout << "Buffers queued (setPositionMS) on source 0: " << queued << "  Buffers processed:" << processed << endl;

	}else{
        //std::unique_lock<std::mutex> lock(mutex);
        for(int i=0;i<(int)sources.size();i++){
            alSourcef(sources[i],AL_SEC_OFFSET,float(ms)/1000.f);
		}
	}
}

//------------------------------------------------------------
float OpenALSoundPlayer::getPosition() const{
	if(duration==0 || sources.empty())
		return 0;
	else
		return getPositionMS()/(1000.f*duration);
}

//------------------------------------------------------------
int OpenALSoundPlayer::getPositionMS() const{
	if(sources.empty()) return 0;
	float pos;
#ifdef FEEDRA_USING_MPG123
	if(mp3streamf){
		pos = float(mpg123_tell(mp3streamf)) / float(samplerate);
	}else
#endif
	if(streamf){
		pos = float(stream_samples_read) / float(channels) / float(samplerate);
	}else{
		alGetSourcef(sources[sources.size()-1],AL_SEC_OFFSET,&pos);
	}
	return pos * 1000.f;
}

//------------------------------------------------------------
void OpenALSoundPlayer::setPan(float p){
	if(sources.empty()) return;
    if(!spatialisedStereo) {
        //Panning does nothing, so exit
        return;
    }

	p = std::clamp(p, -1.f, 1.f);
	pan = p;
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
void OpenALSoundPlayer::setPaused(bool bP){
	if(sources.empty()) return;
    if(!bLoadedOk) return;
    {
        std::unique_lock<std::mutex> lock(mutex);
        bPaused = bP;
        if(bPaused){
            alSourcePausev(sources.size(),&sources[0]);
        }else{
            alSourcePlayv(sources.size(),&sources[0]);
        }
    }
    if(isStreaming){
        if(bPaused){
            stopThread();
            waitForThread();
        }else{
            stream_end = false;
            startThread();
        }
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


//------------------------------------------------------------
void OpenALSoundPlayer::setLoop(bool bLp){
	if(bMultiPlay) return; // no looping on multiplay
	bLoop = bLp;
	if(isStreaming) return;
	for(int i=0;i<(int)sources.size();i++){
		alSourcei(sources[i],AL_LOOPING,bLp?AL_TRUE:AL_FALSE);
	}
}

// ----------------------------------------------------------------------------
void OpenALSoundPlayer::setMultiPlay(bool bMp){
	if(isStreaming && bMp){
		qWarning() << "OpenALSoundPlayer" << "setMultiPlay(): sorry, no support for multiplay streams";
		return;
	}
	bMultiPlay = bMp;		// be careful with this...
	if(sources.empty()) return;
	if(bMultiPlay){
	}else{
		}
}

// ----------------------------------------------------------------------------
void OpenALSoundPlayer::play(){
    if(sources.empty()) return;
    if(!bLoadedOk) return;

    int err = AL_NO_ERROR;
    {
    std::unique_lock<std::mutex> lock(mutex);
	err = alGetError();

	// if the sound is set to multiplay, then create new sources,
	// do not multiplay on loop or we won't be able to stop it
	if (bMultiPlay && !bLoop){
		sources.resize(sources.size()+channels);
		alGetError(); // Clear error.
		alGenSources(channels, &sources[sources.size()-channels]);
		err = alGetError();
		if (err != AL_NO_ERROR){
			qCritical() << "OpenALSoundPlayer" << "play(): couldn't create multiplay stereo sources: "
			<< (int) err << " " << getALErrorString(err).c_str();
			return;
		}
		for(int i=0;i<channels;i++){
			alSourcei (sources[sources.size()-channels+i], AL_BUFFER,   buffers[i]   );
			// only stereo panning
			if(i==0){
				float pos[3] = {-1,0,0};
				alSourcefv(sources[sources.size()-channels+i],AL_POSITION,pos);
			}else{
				float pos[3] = {1,0,0};
				alSourcefv(sources[sources.size()-channels+i],AL_POSITION,pos);
			}
		    alSourcef (sources[sources.size()-channels+i], AL_ROLLOFF_FACTOR,  0.0);
		    alSourcei (sources[sources.size()-channels+i], AL_SOURCE_RELATIVE, AL_TRUE);
		}

		err = alGetError();
		if (err != AL_NO_ERROR){
			qCritical() << "OpenALSoundPlayer" << "play(): couldn't assign multiplay buffers: "
			<< (int) err << " " << getALErrorString(err).c_str();
			return;
		}
	}

    if(bMultiPlay) {
        alSourcePlayv(channels,&sources[sources.size()-channels]);
    } else {
        alSourcePlayv(sources.size(),&sources[sources.size()-channels]);
    }
    }

	if(isStreaming){
		setPosition(0);
		stream_end = false;
		startThread();
	}

}

// ----------------------------------------------------------------------------
void OpenALSoundPlayer::stop(){
    if(sources.empty()) return;
    if(!bLoadedOk) return;

    if(bMultiPlay) {
        std::unique_lock<std::mutex> lock(mutex);
        alSourceStopv(sources.size(),&sources[sources.size()-channels]);
    } else {
        std::unique_lock<std::mutex> lock(mutex);
        alSourceStopv(sources.size(),&sources[0]);
    }

    setPosition(0);

	if(isStreaming){
        stream_end = true;
        //cout << "stopThread()" << endl;
        stopThread();
        setPosition(0);

	}
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
