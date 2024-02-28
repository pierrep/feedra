#include "OpenALSoundPlayer.h"

#include "ofConstants.h"
#include "glm/gtc/constants.hpp"
#include "glm/common.hpp"
#include "ofLog.h"
#include "ofEvents.h"
#include <sndfile.h>

#ifdef OF_USING_MPG123
#include <mpg123.h>
#endif

using namespace std;

static ALCdevice * alDevice = nullptr;
static ALCcontext * alContext = nullptr;
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

#include "AL/alext.h"
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

    switch((format&SF_FORMAT_SUBMASK))
    {
    case SF_FORMAT_PCM_24:
        return "24 bit PCM";
    case SF_FORMAT_PCM_32:
        return "32 bit PCM";
    case SF_FORMAT_FLOAT:
        return "float";
    case SF_FORMAT_DOUBLE:
        return "double";
    case SF_FORMAT_VORBIS:
        return "Ogg/Vorbis";
    case SF_FORMAT_OPUS:
        return "Opus";
    case SF_FORMAT_ALAC_20:
        return "20 bit Apple Lossless Codec";
    case SF_FORMAT_ALAC_24:
        return "24 bit Apple Lossless Codec";
    case SF_FORMAT_ALAC_32:
        return "32 bit Apple Lossless Codec";
    case 0x0080/*SF_FORMAT_MPEG_LAYER_I*/:
        return "MPEG Layer I";
    case 0x0081/*SF_FORMAT_MPEG_LAYER_II*/:
        return "MPEG Layer II";
    case 0x0082/*SF_FORMAT_MPEG_LAYER_III*/:
        return "mp3";
    case SF_FORMAT_IMA_ADPCM:
        return "IMA ADPCM";
    case SF_FORMAT_MS_ADPCM:
        return "Microsoft ADPCM";
    }

    if((format&SF_FORMAT_TYPEMASK) == SF_FORMAT_FLAC) {
        return "FLAC";
    }

    return "Unknown format: " + ofToString(format);
}

#ifdef OF_USING_MPG123
static string getMpg123EncodingString(int encoding) {
	switch(encoding) {
		case MPG123_ENC_16:
			return "MPG123_ENC_16";
#if MPG123_API_VERSION>=36
		case MPG123_ENC_24:
			return "MPG123_ENC_24";
#endif
		case MPG123_ENC_32:
			return "MPG123_ENC_32";
		case MPG123_ENC_8:
			return "MPG123_ENC_8";
		case MPG123_ENC_ALAW_8:
			return "MPG123_ENC_ALAW_8";
		case MPG123_ENC_FLOAT:
			return "MPG123_ENC_FLOAT";
		case MPG123_ENC_FLOAT_32:
			return "MPG123_ENC_FLOAT_32";
		case MPG123_ENC_FLOAT_64:
			return "MPG123_ENC_FLOAT_64";
		case MPG123_ENC_SIGNED:
			return "MPG123_ENC_SIGNED";
		case MPG123_ENC_SIGNED_16:
			return "MPG123_ENC_SIGNED_16";
#if MPG123_API_VERSION>=36
		case MPG123_ENC_SIGNED_24:
			return "MPG123_ENC_SIGNED_24";
#endif
		case MPG123_ENC_SIGNED_32:
			return "MPG123_ENC_SIGNED_32";
		case MPG123_ENC_SIGNED_8:
			return "MPG123_ENC_SIGNED_8";
		case MPG123_ENC_ULAW_8:
			return "MPG123_ENC_ULAW_8";
		case MPG123_ENC_UNSIGNED_16:
			return "MPG123_ENC_UNSIGNED_16";
#if MPG123_API_VERSION>=36
		case MPG123_ENC_UNSIGNED_24:
			return "MPG123_ENC_UNSIGNED_24";
#endif
		case MPG123_ENC_UNSIGNED_32:
			return "MPG123_ENC_UNSIGNED_32";
		case MPG123_ENC_UNSIGNED_8:
			return "MPG123_ENC_UNSIGNED_8";
		default:
			return "MPG123_ENC_ANY";
	}
}
#endif

#define BUFFER_STREAM_SIZE 4096

enum FormatType {
    Int16,
    Float,
    IMA4,
    MSADPCM
};

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
	isStreaming		= false;
	channels		= 0;
	duration		= 0;
	fftCfg			= 0;
	streamf			= 0;
#ifdef OF_USING_MPG123
	mp3streamf		= 0;
#endif
	players().insert(this);
}

// ----------------------------------------------------------------------------
OpenALSoundPlayer::~OpenALSoundPlayer(){
	unload();
	kiss_fftr_free(fftCfg);
	players().erase(this);
	if( players().empty() ){
		close();
	}
    this->waitForThread();
}

int getDevices(const char *type, const char *list, bool printOutput)
{
  ALCchar *ptr, *nptr;
  int num_devices = 0;

  ptr = (ALCchar *)list;
  if(printOutput) ofLogNotice() << "List of all available " << type << " devices: ";
  if (!list)
  {
    if(printOutput) ofLogNotice() << "none";
  }
  else
  {
    nptr = ptr;
    while (*(nptr += strlen(ptr)+1) != 0)
    {
      if(printOutput) ofLogNotice() << "* " << ptr;
      ptr = nptr;
      num_devices++;
    }
    if(printOutput) ofLogNotice() << "* " << ptr;
    num_devices++;
  }

  return num_devices;
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

string OpenALSoundPlayer::getDefaultDevice()
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
        ofLogNotice() << "Default output device name: " << defaultDeviceName;
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
	if( !alDevice ){
        ALCint major, minor;
		alDevice = alcOpenDevice( nullptr );
		if( !alDevice ){
			ofLogError("OpenALSoundPlayer") << "initialize(): couldn't open OpenAL default device";
			return;
        }else{            
            ofLogNotice("OpenALSoundPlayer") << "initialize(): opening "<< alcGetString( alDevice, ALC_DEVICE_SPECIFIER );
            alcGetIntegerv(alDevice, ALC_MAJOR_VERSION, 1, &major);
            alcGetIntegerv(alDevice, ALC_MINOR_VERSION, 1, &minor);            
		}
		// Create OpenAL context and make it current. If fails, close the OpenAL device that was just opened.
		alContext = alcCreateContext( alDevice, nullptr );
		if( !alContext ){
			ALCenum err = alcGetError( alDevice ); 
			ofLogError("OpenALSoundPlayer") << "initialize(): couldn't not create OpenAL context : "<< getALCErrorString( err );
			close();
			return;
		}

		if( alcMakeContextCurrent( alContext )==ALC_FALSE ){
			ALCenum err = alcGetError( alDevice ); 
			ofLogError("OpenALSoundPlayer") << "initialize(): couldn't not make current the create OpenAL context : "<< getALCErrorString( err );
			close();
			return;
		};
		alListener3f( AL_POSITION, 0,0,0 );
#ifdef OF_USING_MPG123
		mpg123_init();
#endif
        ofLogNotice() << "Vendor: \""<<  alGetString(AL_VENDOR) << "\"";
        ofLogNotice() << "Renderer: \""<< alGetString(AL_RENDERER) << "\"";
        ofLogNotice() << "Version: " << alGetString(AL_VERSION);
        ofLogNotice() << "ALC version: " << major << "." << minor;        
        ALCint data[16];
        alcGetIntegerv(alDevice, ALC_FREQUENCY, 1, data);
        ofLogNotice() << "Mixer frequency: " << data[0] << " hz";
        listDevices();

//        alcGetIntegerv(alDevice, ALC_REFRESH, 1, data+1);
//        printf("refresh rate : %u hz\n", data[0]/data[1]);
	}

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
}

//---------------------------------------
void OpenALSoundPlayer::createWindow(int size){
	if(int(window.size())!=size){
		windowSum = 0;
		window.resize(size);
		// hanning window
		for(int i = 0; i < size; i++){
			window[i] = .54 - .46 * cos((glm::two_pi<float>() * i) / (size - 1));
			windowSum += window[i];
		}
	}
}

//---------------------------------------
void OpenALSoundPlayer::close(){
	// Destroy the OpenAL context (if any) before closing the device
	if( alDevice ){
		if( alContext ){
#ifdef OF_USING_MPG123
			mpg123_exit();
#endif
			alcMakeContextCurrent(nullptr);
			alcDestroyContext(alContext);
			alContext = nullptr;
		}
		if( alcCloseDevice( alDevice )==ALC_FALSE ){
			ofLogNotice("OpenALSoundPlayer") << "initialize(): error closing OpenAL device.";
		}
		alDevice = nullptr;
	}
}

// ----------------------------------------------------------------------------
bool OpenALSoundPlayer::sfReadFile(const std::filesystem::path& path, vector<short> & buffer, vector<float> & fftAuxBuffer){
	SF_INFO sfInfo;
	SNDFILE* f = sf_open(path.string().c_str(),SFM_READ,&sfInfo);
	if(!f){
		ofLogError("OpenALSoundPlayer") << "sfReadFile(): couldn't read \"" << path << "\"";
		return false;
	}

	buffer.resize(sfInfo.frames*sfInfo.channels);
	fftAuxBuffer.resize(sfInfo.frames*sfInfo.channels);

	int subformat = sfInfo.format & SF_FORMAT_SUBMASK ;
	if (subformat == SF_FORMAT_FLOAT || subformat == SF_FORMAT_DOUBLE){
		double	scale ;
		sf_command (f, SFC_CALC_SIGNAL_MAX, &scale, sizeof (scale)) ;
		if (scale < 1e-10)
			scale = 1.0 ;
		else
			scale = 32700.0 / scale ;

		sf_count_t samples_read = sf_read_float (f, &fftAuxBuffer[0], fftAuxBuffer.size());
		if(samples_read<(int)fftAuxBuffer.size()){
			ofLogWarning("OpenALSoundPlayer") << "sfReadFile(): read " << samples_read << " float samples, expected "
			<< fftAuxBuffer.size() << " for \"" << path << "\"";
		}
		for (int i = 0 ; i < int(fftAuxBuffer.size()) ; i++){
			fftAuxBuffer[i] *= scale ;
			buffer[i] = 32565.0 * fftAuxBuffer[i];
		}
	}else{
		sf_count_t frames_read = sf_readf_short(f,&buffer[0],sfInfo.frames);
		if(frames_read<sfInfo.frames){
			ofLogError("OpenALSoundPlayer") << "sfReadFile(): read " << frames_read << " frames from buffer, expected "
			<< sfInfo.frames << " for \"" << path << "\"";
			return false;
		}
		sf_seek(f,0,SEEK_SET);
		frames_read = sf_readf_float(f,&fftAuxBuffer[0],sfInfo.frames);
		if(frames_read<sfInfo.frames){
			ofLogError("OpenALSoundPlayer") << "sfReadFile(): read " << frames_read << " frames from fft buffer, expected "
			<< sfInfo.frames << " for \"" << path << "\"";
			return false;
		}
	}
	sf_close(f);

	channels = sfInfo.channels;
	duration = float(sfInfo.frames) / float(sfInfo.samplerate);
	samplerate = sfInfo.samplerate;
	return true;
}

#ifdef OF_USING_MPG123
//------------------------------------------------------------
bool OpenALSoundPlayer::mpg123ReadFile(const std::filesystem::path& path,vector<short> & buffer,vector<float> & fftAuxBuffer){
	int err = MPG123_OK;
	mpg123_handle * f = mpg123_new(nullptr,&err);
	if(mpg123_open(f,path.string().c_str())!=MPG123_OK){
		ofLogError("OpenALSoundPlayer") << "mpg123ReadFile(): couldn't read \"" << path << "\"";
		return false;
	}

	mpg123_enc_enum encoding;
	long int rate;
	mpg123_getformat(f,&rate,&channels,(int*)&encoding);
	if(encoding!=MPG123_ENC_SIGNED_16){
		ofLogError("OpenALSoundPlayer") << "mpg123ReadFile(): " << getMpg123EncodingString(encoding)
			<< " encoding for \"" << path << "\"" << " unsupported, expecting MPG123_ENC_SIGNED_16";
		return false;
	}
	samplerate = rate;

	size_t done=0;
	size_t buffer_size = mpg123_outblock( f );
	buffer.resize(buffer_size/2);
	while(mpg123_read(f,(unsigned char*)&buffer[buffer.size()-buffer_size/2],buffer_size,&done)!=MPG123_DONE){
		buffer.resize(buffer.size()+buffer_size/2);
	};
	buffer.resize(buffer.size()-(buffer_size/2-done/2));
	mpg123_close(f);
	mpg123_delete(f);

	fftAuxBuffer.resize(buffer.size());
	for(int i=0;i<(int)buffer.size();i++){
		fftAuxBuffer[i] = float(buffer[i])/32565.f;
	}
	duration = float(buffer.size()/channels) / float(samplerate);
	return true;
}
#endif

//------------------------------------------------------------
bool OpenALSoundPlayer::sfStream(const std::filesystem::path& path,vector<short> & buffer,vector<float> & fftAuxBuffer){
	if(!streamf){
		SF_INFO sfInfo;
		streamf = sf_open(path.string().c_str(),SFM_READ,&sfInfo);
		if(!streamf){
			ofLogError("OpenALSoundPlayer") << "sfStream(): couldn't read \"" << path << "\"";
			return false;
		}

		stream_subformat = sfInfo.format & SF_FORMAT_SUBMASK ;
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
	buffer.resize(curr_buffer_size);
	fftAuxBuffer.resize(buffer.size());
	if (stream_subformat == SF_FORMAT_FLOAT || stream_subformat == SF_FORMAT_DOUBLE){
		sf_count_t samples_read = sf_read_float (streamf, &fftAuxBuffer[0], fftAuxBuffer.size());
		stream_samples_read += samples_read;
		if(samples_read<(int)fftAuxBuffer.size()){
			fftAuxBuffer.resize(samples_read);
			buffer.resize(samples_read);

            // set to start of stream
            sf_seek(streamf,0,SEEK_SET);

			if(!bLoop) stopThread();
			stream_samples_read = 0;
			stream_end = true;
		}
		for (int i = 0 ; i < int(fftAuxBuffer.size()) ; i++){
            fftAuxBuffer[i] *= stream_scale ;
			buffer[i] = 32565.0 * fftAuxBuffer[i];
		}
	}else{
		sf_count_t frames_read = sf_readf_short(streamf,&buffer[0],curr_buffer_size/channels);
		stream_samples_read += frames_read*channels;
        //cout << "sfStream()   frames_read = " << frames_read << " stream_samples_read: " << stream_samples_read << endl;
        if(frames_read < curr_buffer_size/channels){
			fftAuxBuffer.resize(frames_read*channels);
			buffer.resize(frames_read*channels);

            // set to start of stream
            sf_seek(streamf,0,SEEK_SET);

			if(!bLoop) stopThread();
			stream_samples_read = 0;
            //cout << "End of stream, stream_samples_read = 0" << endl;
			stream_end = true;
		}
		for(int i=0;i<(int)buffer.size();i++){
			fftAuxBuffer[i]=float(buffer[i])/32565.0f;
		}
	}

	return true;
}

#ifdef OF_USING_MPG123
//------------------------------------------------------------
bool OpenALSoundPlayer::mpg123Stream(const std::filesystem::path& path,vector<short> & buffer,vector<float> & fftAuxBuffer){
	if(!mp3streamf){
		int err = MPG123_OK;
		mp3streamf = mpg123_new(nullptr,&err);
		if(mpg123_open(mp3streamf,path.string().c_str())!=MPG123_OK){
			mpg123_close(mp3streamf);
			mpg123_delete(mp3streamf);
            mp3streamf = 0;
            ofLogError("OpenALSoundPlayer") << "mpg123Stream(): couldn't read " << path;
			return false;
		}

		long int rate;
		mpg123_getformat(mp3streamf,&rate,&channels,(int*)&stream_encoding);
		if(stream_encoding!=MPG123_ENC_SIGNED_16){
			ofLogError("OpenALSoundPlayer") << "mpg123Stream(): " << getMpg123EncodingString(stream_encoding)
			<< " encoding for \"" << path << "\"" << " unsupported, expecting MPG123_ENC_SIGNED_16";
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
	buffer.resize(curr_buffer_size);
	fftAuxBuffer.resize(buffer.size());
	size_t done=0;
	if(mpg123_read(mp3streamf,(unsigned char*)&buffer[0],curr_buffer_size*2,&done)==MPG123_DONE){
        //set to start of stream
        mpg123_seek(mp3streamf,0,SEEK_SET);

		buffer.resize(done/2);
		fftAuxBuffer.resize(done/2);
		if(!bLoop) stopThread();
		stream_end = true;
	}


	for(int i=0;i<(int)buffer.size();i++){
		fftAuxBuffer[i] = float(buffer[i])/32565.f;
	}

	return true;
}
#endif

//------------------------------------------------------------
bool OpenALSoundPlayer::stream(const std::filesystem::path& fileName, vector<short> & buffer){
#ifdef OF_USING_MPG123
	if(ofFilePath::getFileExt(fileName)=="mp3" || ofFilePath::getFileExt(fileName)=="MP3" || mp3streamf){
		if(!mpg123Stream(fileName,buffer,fftAuxBuffer)) return false;
	}else
#endif
		if(!sfStream(fileName,buffer,fftAuxBuffer)) return false;

	fftBuffers.resize(channels);
	int numFrames = buffer.size()/channels;

	for(int i=0;i<channels;i++){
		fftBuffers[i].resize(numFrames);
		for(int j=0;j<numFrames;j++){
			fftBuffers[i][j] = fftAuxBuffer[j*channels+i];
		}
	}
	return true;
}

bool OpenALSoundPlayer::readFile(const std::filesystem::path& fileName, vector<short> & buffer){
#ifdef OF_USING_MPG123
	if(ofFilePath::getFileExt(fileName)!="mp3" && ofFilePath::getFileExt(fileName)!="MP3"){
		if(!sfReadFile(fileName,buffer,fftAuxBuffer)) return false;
	}else{
		if(!mpg123ReadFile(fileName,buffer,fftAuxBuffer)) return false;
	}
#else
	if(!sfReadFile(fileName,buffer,fftAuxBuffer)) return false;
#endif
	fftBuffers.resize(channels);
	int numFrames = buffer.size()/channels;

	for(int i=0;i<channels;i++){
		fftBuffers[i].resize(numFrames);
		for(int j=0;j<numFrames;j++){
			fftBuffers[i][j] = fftAuxBuffer[j*channels+i];
		}
	}
	return true;
}

//------------------------------------------------------------
bool OpenALSoundPlayer::load(const std::filesystem::path& _fileName, bool is_stream){

	std::filesystem::path fileName = ofToDataPath(_fileName);
    enum FormatType sample_format = Int16;
	bMultiPlay = false;
	isStreaming = is_stream;
	int err = AL_NO_ERROR;

	// [1] init sound systems, if necessary
	initialize();

	// [2] try to unload any previously loaded sounds
	// & prevent user-created memory leaks
	// if they call "loadSound" repeatedly, for example
	unload();
    bLoadedOk = false;

    // Get Format
    SF_INFO sfInfo;

    if(ofFilePath::getFileExt(fileName)=="mp3" || ofFilePath::getFileExt(fileName)=="MP3"){
        fileformat = 0x0082;
    } else {
        SNDFILE* f = sf_open(_fileName.string().c_str(),SFM_READ,&sfInfo);
        fileformat = sfInfo.format;
        /* Detect a suitable format to load. Formats like Vorbis and Opus use float
         * natively, so load as float to avoid clipping when possible. Formats
         * larger than 16-bit can also use float to preserve a bit more precision.
         */
        switch((sfInfo.format&SF_FORMAT_SUBMASK))
        {
            case SF_FORMAT_PCM_24:
            case SF_FORMAT_PCM_32:
            case SF_FORMAT_FLOAT:
            case SF_FORMAT_DOUBLE:
            case SF_FORMAT_VORBIS:
            case SF_FORMAT_OPUS:
            case SF_FORMAT_ALAC_20:
            case SF_FORMAT_ALAC_24:
            case SF_FORMAT_ALAC_32:
            case 0x0080/*SF_FORMAT_MPEG_LAYER_I*/:
            case 0x0081/*SF_FORMAT_MPEG_LAYER_II*/:
            case 0x0082/*SF_FORMAT_MPEG_LAYER_III*/:
                if(alIsExtensionPresent("AL_EXT_FLOAT32"))
                    sample_format = Float;
                break;
        }
        sf_close(f);
    }
    format_string = getSoundFileFormatString(fileformat);    

    ALenum openALformat=AL_FORMAT_MONO16;

	if(!isStreaming){
		readFile(fileName, buffer);
	}else{
        //read stream to get buffer size
		stream(fileName, buffer);
	}

    if(buffer.size() == 0)
    {
        ofLogError() << "Sound file load failed - wrong file type or empty file";
        return false;
    }

    if(sample_format == Int16)
    {
        splblockalign = 1;
        byteblockalign = channels * 2;
    }
    else if(sample_format == Float)
    {
        splblockalign = 1;
        byteblockalign = channels * 4;
    }

    /* Figure out the OpenAL format from the file and desired sample type. */
    openALformat = AL_NONE;
    if(channels == 1)
    {
        if(sample_format == Int16)
            openALformat = AL_FORMAT_MONO16;
        else if(sample_format == Float)
            openALformat = AL_FORMAT_MONO_FLOAT32;
    }
    else if(channels == 2)
    {
        if(sample_format == Int16)
            openALformat = AL_FORMAT_STEREO16;
        else if(sample_format == Float)
            openALformat = AL_FORMAT_STEREO_FLOAT32;
    }
    //ofLogNotice() << "OpenAL Format should be " << getOpenALFormatString(openALformat);

	int numFrames = buffer.size()/channels;

	if(isStreaming){
		buffers.resize(channels*2);
	}else{
		buffers.resize(channels);
	}
	alGenBuffers(buffers.size(), &buffers[0]);
    //cout << "sound load -> channels: "<< channels << " buffers.size: " << buffers.size() << " buffer.size(): " << buffer.size() << " duration: " << duration << endl;
	if(channels==1){
		sources.resize(1);
		alGetError(); // Clear error.
		alGenSources(1, &sources[0]);
		err = alGetError();
		if (err != AL_NO_ERROR){
			ofLogError("OpenALSoundPlayer") << "loadSound(): couldn't generate source for \"" << fileName << "\": "
			<< (int) err << " " << getALErrorString(err);
			return false;
		}

        if(isStreaming){
            //reset back to zero
            setPosition(0);
        }

		for(int i=0; i<(int)buffers.size(); i++){
            if(isStreaming){
                stream(fileName,buffer);
            }

			alGetError(); // Clear error.
            openALformat=AL_FORMAT_MONO16;
            alBufferData(buffers[i],openALformat,&buffer[0],buffer.size()*2,samplerate);
			err = alGetError();
			if (err != AL_NO_ERROR){
				ofLogError("OpenALSoundPlayer:") << "loadSound(): couldn't create buffer for \"" << fileName << "\": "
				<< (int) err << " " << getALErrorString(err);
				return false;
			}
		}
		if(isStreaming){
            alSourceQueueBuffers(sources[0],buffers.size(),&buffers[0]);
		}else{
			alSourcei (sources[0], AL_BUFFER,   buffers[0]);
		}

		alSourcef (sources[0], AL_PITCH,    1.0f);
		alSourcef (sources[0], AL_GAIN,     1.0f);
	    alSourcef (sources[0], AL_ROLLOFF_FACTOR,  0.0);
	    alSourcei (sources[0], AL_SOURCE_RELATIVE, AL_TRUE);
	}else{
		vector<vector<short> > multibuffer;
		multibuffer.resize(channels);
		sources.resize(channels);
		alGenSources(channels, &sources[0]);

        if(isStreaming){
            //reset back to zero
            setPosition(0);
        }

		if(isStreaming){
            for(int s=0; s < 2;s++)
            {
                stream(fileName,buffer);
                for(int i=0;i<channels;i++){

                    multibuffer[i].resize(buffer.size()/channels);
                    for(int j=0;j<numFrames;j++){
                        multibuffer[i][j] = buffer[j*channels+i];
                    }
                    alGetError(); // Clear error.
                    openALformat = AL_FORMAT_MONO16;
                    alBufferData(buffers[s*2+i],openALformat,&multibuffer[i][0],buffer.size()/channels*2,samplerate);
                    err = alGetError();
                    if ( err != AL_NO_ERROR){
                        ofLogError("OpenALSoundPlayer") << "loadSound(): couldn't create stereo buffers for \"" << fileName << "\": " << (int) err << " " << getALErrorString(err);
                        sources.clear();
                        multibuffer.clear();
                        return false;
                    }
                    alSourceQueueBuffers(sources[i],1,&buffers[s*2+i]);
                }
            }
		}else{
			for(int i=0;i<channels;i++){
				multibuffer[i].resize(buffer.size()/channels);
				for(int j=0;j<numFrames;j++){
					multibuffer[i][j] = buffer[j*channels+i];
				}
				alGetError(); // Clear error.
                alBufferData(buffers[i],openALformat,&multibuffer[i][0],buffer.size()/channels*2,samplerate);
				err = alGetError();
				if (err != AL_NO_ERROR){
					ofLogError("OpenALSoundPlayer") << "loadSound(): couldn't create stereo buffers for \"" << fileName << "\": "
					<< (int) err << " " << getALErrorString(err);
					return false;
				}
				alSourcei (sources[i], AL_BUFFER,   buffers[i]   );
			}
		}


        for(int i=0;i<channels;i++){
            err = alGetError();
            if (err != AL_NO_ERROR){
                ofLogError("OpenALSoundPlayer") << "loadSound(): couldn't create stereo sources for \"" << fileName << "\": "
                << (int) err << " " << getALErrorString(err);
                return false;
            }

            // only stereo panning
            if(i==0){
                float pos[3] = {-1,0,0};
                alSourcefv(sources[i],AL_POSITION,pos);
            }else{
                float pos[3] = {1,0,0};
                alSourcefv(sources[i],AL_POSITION,pos);
            }
            alSourcef (sources[i], AL_ROLLOFF_FACTOR,  0.0);
            alSourcei (sources[i], AL_SOURCE_RELATIVE, AL_TRUE);
        }
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
	vector<vector<short> > multibuffer;
	multibuffer.resize(channels);
	while(isThreadRunning()){
        sleep(1);
		std::unique_lock<std::mutex> lock(mutex);

		for(int i=0; i<int(sources.size())/channels; i++){
            ALint state;
            alGetSourcei(sources[i*channels],AL_SOURCE_STATE,&state);

			int processed;
			alGetSourcei(sources[i*channels], AL_BUFFERS_PROCESSED, &processed);
            while(processed)
			{
                //cout << "source: " << i*channels << " AL_BUFFERS_PROCESSED = " << processed << endl;
                processed--;
                stream("",buffer);
				int numFrames = buffer.size()/channels;
				if(channels>1){
					for(int j=0;j<channels;j++){
						multibuffer[j].resize(buffer.size()/channels);
						for(int k=0;k<numFrames;k++){
							multibuffer[j][k] = buffer[k*channels+j];
						}
						ALuint albuffer;
						alSourceUnqueueBuffers(sources[i*channels+j], 1, &albuffer);                        
                        alBufferData(albuffer,AL_FORMAT_MONO16,&multibuffer[j][0],buffer.size()*2/channels,samplerate);
                        alSourceQueueBuffers(sources[i*channels+j], 1, &albuffer);
                        //cout << "sourceQueuebuffer(thread)-> sources: " << i*channels+j << endl;
					}
				}else{
					ALuint albuffer;
					alSourceUnqueueBuffers(sources[i], 1, &albuffer);
                    alBufferData(albuffer,AL_FORMAT_MONO16,&buffer[0],buffer.size()*2/channels,samplerate);
					alSourceQueueBuffers(sources[i], 1, &albuffer);
                    //cout << "sourceQueuebuffer(thread)-> sources: " << i << endl;
				}
                if(stream_end && !(state == AL_STOPPED)){
                    //cout << "threadedFunction() - stream end! state: " << state << endl;
					break;
				}
			}

			bool stream_running=false;
			#ifdef OF_USING_MPG123
				stream_running = streamf || mp3streamf;
			#else
				stream_running = streamf;
			#endif
			if(state != AL_PLAYING && stream_running && !stream_end){
                alSourcePlayv(channels,&sources[i*channels]);
                cout << "Loop stream!" << endl;
                stream_end = false;
			}

            //stream_end = false;
        }
	}
}

//------------------------------------------------------------
void OpenALSoundPlayer::update(ofEventArgs & args){

	for(int i=1; i<int(sources.size())/channels; ){
		ALint state;
		alGetSourcei(sources[i*channels],AL_SOURCE_STATE,&state);

        ALdouble offsets[2];
        alGetSourcedvSOFT(sources[i*channels], AL_SEC_OFFSET_LATENCY_SOFT, offsets);
        ofLogVerbose() << " Offset: " << offsets[0] << " - Latency: " << (ALuint)(offsets[1]*1000) << " ms";
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

//------------------------------------------------------------
void OpenALSoundPlayer::unload(){
	stop();
	ofRemoveListener(ofEvents().update,this,&OpenALSoundPlayer::update);

	// Only lock the thread where necessary.
	{
		std::unique_lock<std::mutex> lock(mutex);

		// Delete sources before buffers.
		alDeleteSources(sources.size(),&sources[0]);
		alDeleteBuffers(buffers.size(),&buffers[0]);

		sources.clear();
		buffers.clear();
	}

	// Free resources and close file descriptors.
#ifdef OF_USING_MPG123
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
	if(channels==1){
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

#ifdef OF_USING_MPG123
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
        std::unique_lock<std::mutex> lock(mutex);
		for(int i=0;i<(int)channels;i++){
			alSourcef(sources[sources.size()-channels+i],AL_SEC_OFFSET,float(ms)/1000.f);
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
#ifdef OF_USING_MPG123
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
	p = glm::clamp(p, -1.f, 1.f);
	pan = p;
	if(channels==1){
		float pos[3] = {p,0,0};
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
				alSourcef(sources[sources.size()-channels+i],AL_GAIN,leftVol*volume);
			}else{
				alSourcef(sources[sources.size()-channels+i],AL_GAIN,rightVol*volume);
			}
		}
	}
}


//------------------------------------------------------------
void OpenALSoundPlayer::setPaused(bool bP){
	if(sources.empty()) return;
    std::unique_lock<std::mutex> lock(mutex);
    bPaused = bP;
    if(bPaused){
		alSourcePausev(sources.size(),&sources[0]);
        if(isStreaming){
            stopThread();
		}
	}else{
		alSourcePlayv(sources.size(),&sources[0]);
        if(isStreaming){
            stream_end = false;
            startThread();
		}
	}
}


//------------------------------------------------------------
void OpenALSoundPlayer::setSpeed(float spd){
	for(int i=0;i<channels;i++){
		alSourcef(sources[sources.size()-channels+i],AL_PITCH,spd);
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
		ofLogWarning("OpenALSoundPlayer") << "setMultiPlay(): sorry, no support for multiplay streams";
		return;
	}
	bMultiPlay = bMp;		// be careful with this...
	if(sources.empty()) return;
	if(bMultiPlay){
		ofAddListener(ofEvents().update,this,&OpenALSoundPlayer::update);
	}else{
		ofRemoveListener(ofEvents().update,this,&OpenALSoundPlayer::update);
	}
}

// ----------------------------------------------------------------------------
void OpenALSoundPlayer::play(){
    std::unique_lock<std::mutex> lock(mutex);
	int err = alGetError();

	// if the sound is set to multiplay, then create new sources,
	// do not multiplay on loop or we won't be able to stop it
	if (bMultiPlay && !bLoop){
		sources.resize(sources.size()+channels);
		alGetError(); // Clear error.
		alGenSources(channels, &sources[sources.size()-channels]);
		err = alGetError();
		if (err != AL_NO_ERROR){
			ofLogError("OpenALSoundPlayer") << "play(): couldn't create multiplay stereo sources: "
			<< (int) err << " " << getALErrorString(err);
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

		err = glGetError();
		if (err != AL_NO_ERROR){
			ofLogError("OpenALSoundPlayer") << "play(): couldn't assign multiplay buffers: "
			<< (int) err << " " << getALErrorString(err);
			return;
		}
	}

    alSourcePlayv(channels,&sources[sources.size()-channels]);

	if(bMultiPlay){
		ofAddListener(ofEvents().update,this,&OpenALSoundPlayer::update);
	}
	if(isStreaming){
		setPosition(0);
		stream_end = false;
		startThread();
	}

}

// ----------------------------------------------------------------------------
void OpenALSoundPlayer::stop(){
    //cout << "stop()" << endl;
	if(sources.empty()) return;
    {
        std::unique_lock<std::mutex> lock(mutex);
        alSourceStopv(channels,&sources[sources.size()-channels]);
    }
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
