#include "AudioDevice.h"

#include "AudioCodec.h"

#include <QDebug>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <climits>
#include <cstring>
#include <vector>

#include "AL/alext.h"
#include "AL/efx.h"
#include "AL/efx-presets.h"

using std::string;

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
static constexpr int kEaxBuses = AudioDevice::kEaxReverbCount;
static constexpr int kBusCount = AudioDevice::kSendCount;
static bool bUseEffects = false;
static bool bUseConvolution = false;
static int g_slotCount = 0;
static int g_sendCount = 0; // aux sends each source actually has
static ALuint irBuffer[AudioDevice::kConvolutionCount] = { 0, 0 };
static float g_convolutionGain[AudioDevice::kConvolutionCount] = { 1.0f / 16.0f, 1.0f / 16.0f };
static std::filesystem::path g_irPath[AudioDevice::kConvolutionCount];
static ALuint effects[kBusCount] = { 0, 0, 0, 0 };
static ALuint effectSlots[kBusCount] = { 0, 0, 0, 0 };
static EFXEAXREVERBPROPERTIES reverbs[kEaxBuses] = {
    EFX_REVERB_PRESET_ALLEY,
    EFX_REVERB_PRESET_ALLEY
};


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
// Stereo panning by moving the two channels (OpenAL Soft's AL_EXT_STEREO_ANGLES).
#define FEEDRA_AL_STEREO_ANGLES 0x1030
static bool g_stereoAngles = false;



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

static ALuint loadImpulseBuffer(const std::filesystem::path& filename)
{
    SF_INFO sfinfo;
    std::memset(&sfinfo, 0, sizeof(sfinfo));
    SNDFILE* sndfile = AudioCodec::sfOpenRead(filename, &sfinfo);
    if (!sndfile) {
        qWarning() << "Could not open impulse response" << AudioCodec::pathUtf8(filename).c_str() << sf_strerror(nullptr);
        return 0;
    }
    if (sfinfo.frames < 1 || sfinfo.frames > static_cast<sf_count_t>(INT_MAX / sizeof(float)) / sfinfo.channels) {
        qWarning() << "Bad sample count in impulse response" << AudioCodec::pathUtf8(filename).c_str() << sfinfo.frames;
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

    qInfo() << "Loading impulse response:" << AudioCodec::pathUtf8(filename).c_str()
            << sfinfo.samplerate << "hz" << static_cast<long long>(sfinfo.frames) << "frames";

    std::vector<float> samples(static_cast<size_t>(sfinfo.frames * sfinfo.channels));
    const sf_count_t numFrames = sf_readf_float(sndfile, samples.data(), sfinfo.frames);
    sf_close(sndfile);
    if (numFrames < 1) {
        qWarning() << "Failed to read impulse response" << AudioCodec::pathUtf8(filename).c_str();
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

int AudioDevice::reverbPresetCount()
{
    return static_cast<int>(sizeof(kReverbPresets) / sizeof(kReverbPresets[0]));
}

std::string AudioDevice::reverbPresetId(int index)
{
    if (index < 0 || index >= reverbPresetCount()) {
        return {};
    }
    return kReverbPresets[index].id;
}

std::string AudioDevice::reverbPresetLabel(int index)
{
    if (index < 0 || index >= reverbPresetCount()) {
        return {};
    }
    return humanizeReverbId(kReverbPresets[index].id);
}

int AudioDevice::reverbPresetIndex(int which)
{
    which = std::clamp(which, 0, kEaxBuses - 1);
    if (g_reverbIndex[which] < 0) {
        g_reverbIndex[which] = alleyPresetIndex();
    }
    return g_reverbIndex[which];
}

void AudioDevice::setReverbPreset(int index, int which)
{
    if (index < 0 || index >= reverbPresetCount() || which < 0 || which >= kEaxBuses) {
        return;
    }
    g_reverbIndex[which] = index;
    applyReverbPreset(index, which);
}

bool AudioDevice::setReverbPresetById(const std::string& id, int which)
{
    for (int i = 0; i < reverbPresetCount(); ++i) {
        if (id == kReverbPresets[i].id) {
            setReverbPreset(i, which);
            return true;
        }
    }
    return false;
}

bool AudioDevice::sendAvailable(int bus)
{
    if (!bUseEffects || bus < 0 || bus >= kBusCount || bus >= g_sendCount || effectSlots[bus] == 0) {
        return false;
    }
    return bus < kEaxBuses || bUseConvolution;
}

float AudioDevice::defaultConvolutionGain()
{
    return 1.0f / 16.0f;
}

float AudioDevice::convolutionGain(int which)
{
    return g_convolutionGain[std::clamp(which, 0, AudioDevice::kConvolutionCount - 1)];
}

void AudioDevice::setConvolutionGain(float gain, int which)
{
    if (which < 0 || which >= AudioDevice::kConvolutionCount) {
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

bool AudioDevice::convolutionAvailable()
{
    return bUseConvolution;
}

std::filesystem::path AudioDevice::convolutionImpulsePath(int which)
{
    return g_irPath[std::clamp(which, 0, AudioDevice::kConvolutionCount - 1)];
}

bool AudioDevice::setConvolutionImpulse(const std::filesystem::path& path, int which)
{
    if (!bUseConvolution || path.empty() || which < 0 || which >= AudioDevice::kConvolutionCount) {
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


//------------------------------------------------------------
// Device
//------------------------------------------------------------
namespace {

void printExtensions(const char* header, const char* extensions)
{
    if (!extensions) {
        return;
    }
    qInfo() << header << extensions;
}

void logDevices()
{
    const bool all = alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT") == ALC_TRUE;
    const char* list = alcGetString(nullptr, all ? ALC_ALL_DEVICES_SPECIFIER : ALC_DEVICE_SPECIFIER);
    qInfo() << "Output devices:";
    for (const char* p = list; p && *p; p += std::strlen(p) + 1) {
        qInfo() << "  " << p;
    }
    const char* def = alcGetString(nullptr, all ? ALC_DEFAULT_ALL_DEVICES_SPECIFIER : ALC_DEFAULT_DEVICE_SPECIFIER);
    qInfo() << "Default output device:" << (def ? def : "(none)");
    printExtensions("OpenAL extensions:", alGetString(AL_EXTENSIONS));
    printExtensions("ALC extensions:", alcGetString(alDevice, ALC_EXTENSIONS));
}

} // namespace

bool AudioDevice::isOpen()
{
    return alContext != nullptr;
}

void AudioDevice::process()
{
    if (alContext) {
        alcProcessContext(alContext);
    }
}

std::string AudioDevice::defaultDeviceName()
{
    const char* name = alcGetString(nullptr, ALC_DEFAULT_ALL_DEVICES_SPECIFIER);
    return name ? name : std::string();
}

std::vector<std::string> AudioDevice::outputDevices()
{
    const bool all = alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT") == ALC_TRUE;
    const char* list = alcGetString(nullptr, all ? ALC_ALL_DEVICES_SPECIFIER : ALC_DEVICE_SPECIFIER);
    std::vector<std::string> names;
    for (const char* p = list; p && *p; p += std::strlen(p) + 1) {
        names.emplace_back(p);
    }
    return names;
}

std::string AudioDevice::currentDeviceName()
{
    if (!alDevice) {
        return std::string();
    }
    const bool all = alcIsExtensionPresent(alDevice, "ALC_ENUMERATE_ALL_EXT") == ALC_TRUE;
    const char* name = alcGetString(alDevice, all ? ALC_ALL_DEVICES_SPECIFIER : ALC_DEVICE_SPECIFIER);
    return name ? name : std::string();
}

bool AudioDevice::isConnected()
{
    if (!alDevice) {
        return false;
    }
    if (alcIsExtensionPresent(alDevice, "ALC_EXT_disconnect") != ALC_TRUE) {
        return true;
    }
    ALCint connected = 1;
    alcGetIntegerv(alDevice, ALC_CONNECTED, 1, &connected);
    return connected != 0;
}

double AudioDevice::outputRefreshHz()
{
    if (!alDevice) {
        return 0.0;
    }
    ALCint refresh = 0;
    alcGetIntegerv(alDevice, ALC_REFRESH, 1, &refresh);
    return refresh > 0 ? static_cast<double>(refresh) : 0.0;
}

int AudioDevice::reopen(const char* deviceName)
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


//---------------------------------------
// this should only be called once
bool AudioDevice::initialize(){
    if (alDevice) {
        if (alContext) {
            g_alFloat32.store(alIsExtensionPresent("AL_EXT_FLOAT32") == AL_TRUE);
        }
        return alContext != nullptr;
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
			qCritical() << "AudioDevice" << "initialize(): couldn't open OpenAL default device";
			return false;
        }else{            
            qInfo() << "AudioDevice" << "initialize(): opening "<< alcGetString( alDevice, ALC_DEVICE_SPECIFIER );
            alcGetIntegerv(alDevice, ALC_MAJOR_VERSION, 1, &major);
            alcGetIntegerv(alDevice, ALC_MINOR_VERSION, 1, &minor);            
		}
		// Create OpenAL context and make it current. If fails, close the OpenAL device that was just opened.
        int attrlist[] = { ALC_MAX_AUXILIARY_SENDS, 4, ALC_MONO_SOURCES, 1024, ALC_STEREO_SOURCES, 256, 0 };
        alContext = alcCreateContext( alDevice, attrlist );
		if( !alContext ){
			ALCenum err = alcGetError( alDevice ); 
			qCritical() << "AudioDevice" << "initialize(): couldn't not create OpenAL context : "<< getALCErrorString( err ).c_str();
			close();
			return false;
		}

		if( alcMakeContextCurrent( alContext )==ALC_FALSE ){
			ALCenum err = alcGetError( alDevice ); 
			qCritical() << "AudioDevice" << "initialize(): couldn't not make current the create OpenAL context : "<< getALCErrorString( err ).c_str();
			close();
			return false;
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
        g_stereoAngles = alIsExtensionPresent("AL_EXT_STEREO_ANGLES") == AL_TRUE;
        qInfo() << "Stereo panning" << (g_stereoAngles ? "by stereo angles" : "in the renderer");

        qInfo() << "Vendor: \""<<  alGetString(AL_VENDOR) << "\"";
        qInfo() << "Renderer: \""<< alGetString(AL_RENDERER) << "\"";
        qInfo() << "Version: " << alGetString(AL_VERSION);
        qInfo() << "ALC version: " << major << "." << minor;        
        ALCint data[16];
        alcGetIntegerv(alDevice, ALC_FREQUENCY, 1, data);
        qInfo() << "Mixer sample rate: " << data[0] << " hz";
        logDevices();

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
                    return false;
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
                // Read outside the assert so Release builds clear the error too.
                const ALenum slotErr = alGetError();
                if (slotErr != AL_NO_ERROR) {
                    qWarning() << "Failed to set effect slot:" << getALErrorString(slotErr).c_str();
                }
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
    return alContext != nullptr;
}

//---------------------------------------
void AudioDevice::close(){
	// Destroy the OpenAL context (if any) before closing the device
	if( alDevice ){
		if( alContext ){
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
			qInfo() << "AudioDevice" << "close(): error closing OpenAL device.";
		}
		alDevice = nullptr;
	}
}


//------------------------------------------------------------
// Clock, scheduled starts and source queries
//------------------------------------------------------------
bool AudioDevice::scheduledStartAvailable()
{
    return g_playAtTimev != nullptr && g_getInteger64v != nullptr && alGetSourcei64vSOFT != nullptr && alDevice != nullptr;
}

int64_t AudioDevice::clockNs()
{
    if (!g_getInteger64v || !alDevice) {
        return 0;
    }
    int64_t value = 0;
    g_getInteger64v(alDevice, FEEDRA_ALC_DEVICE_CLOCK_SOFT, 1, &value);
    return value;
}


bool AudioDevice::playAtTime(int count, const ALuint* sources, int64_t startNs)
{
    if (!g_playAtTimev || count <= 0) {
        return false;
    }
    alGetError();
    g_playAtTimev(static_cast<ALsizei>(count), sources, startNs);
    return alGetError() == AL_NO_ERROR;
}

bool AudioDevice::offsetLatency(ALuint source, double& offsetSec, double& latencySec)
{
    if (!alGetSourcedvSOFT) {
        return false;
    }
    ALdouble values[2] = { 0.0, 0.0 };
    alGetSourcedvSOFT(source, AL_SEC_OFFSET_LATENCY_SOFT, values);
    offsetSec = values[0];
    latencySec = values[1];
    return true;
}

bool AudioDevice::sampleOffsetClock(ALuint source, int64_t& offsetFixed, int64_t& clockNsOut)
{
    if (!alGetSourcei64vSOFT) {
        return false;
    }
    ALint64SOFT values[2] = { 0, 0 };
    alGetSourcei64vSOFT(source, FEEDRA_AL_SAMPLE_OFFSET_CLOCK_SOFT, values);
    offsetFixed = static_cast<int64_t>(values[0]);
    clockNsOut = static_cast<int64_t>(values[1]);
    return true;
}

bool AudioDevice::floatOutput()
{
    return g_alFloat32.load();
}

bool AudioDevice::stereoAngles()
{
    return g_stereoAngles;
}

std::string AudioDevice::alErrorString(ALenum error)
{
    return getALErrorString(error);
}

//------------------------------------------------------------
// Send filters
//------------------------------------------------------------
ALuint AudioDevice::effectSlot(int bus)
{
    return bus >= 0 && bus < kBusCount ? effectSlots[bus] : 0;
}

ALuint AudioDevice::newSendFilter()
{
    if (!bUseEffects || !alGenFilters) {
        return 0;
    }
    ALuint filter = 0;
    alGenFilters(1, &filter);
    alFilteri(filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
    return filter;
}

void AudioDevice::setSendFilterGain(ALuint filter, float gain)
{
    if (filter != 0 && alFilterf) {
        alFilterf(filter, AL_LOWPASS_GAIN, gain);
    }
}

void AudioDevice::deleteFilter(ALuint& filter)
{
    if (filter != 0 && alDeleteFilters) {
        alDeleteFilters(1, &filter);
    }
    filter = 0;
}
