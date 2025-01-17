#pragma once

#include "ofConstants.h"

#include "ofSoundBaseTypes.h"
#include "ofThread.h"
#include "ofEvent.h"

#if defined (TARGET_OF_IOS) || defined (TARGET_OSX)
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif


typedef unsigned int ALuint;

#include "kiss_fft.h"
#include "kiss_fftr.h"

#ifdef _WIN32
#include "sndfile.h"
#else
#include <sndfile.h>
#endif


#ifdef _WIN32
//typedef	struct SNDFILE_tag	SNDFILE ;
#endif

enum FormatType {
    Int16,
    Float,
    IMA4,
    MSADPCM
};

#ifdef OF_USING_MPG123
	typedef struct mpg123_handle_struct mpg123_handle;
#endif

class ofEventArgs;

void OpenALSoundUpdate();	

// --------------------- player functions:
class OpenALSoundPlayer : public ofBaseSoundPlayer, public ofThread {

	public:

		OpenALSoundPlayer();
		virtual ~OpenALSoundPlayer();

        static std::string getDefaultDevice();
        static int reopenDevice(const char* devicename);
        static int listDevices(bool printOutput = true);
        static void printExtensions (const char *header, char separator, const char *extensions);
        bool load(const std::filesystem::path& fileName, bool stream = false);
		void unload();
		void play();
		void stop();

		void setVolume(float vol);
		void setPan(float vol); // -1 to 1
		void setSpeed(float spd);
		void setPaused(bool bP);
		void setLoop(bool bLp);
		void setMultiPlay(bool bMp);
		void setPosition(float pct); // 0 = start, 1 = end;
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

        float getDuration() const {return duration;}
        int getSampleRate() const {return samplerate;}
        int getNumChannels() const {return channels;}
        bool isStreamEnd() const { return stream_end;}
        float getReverbSend() const { return reverbSend;}
        void setReverbSend(float send) { reverbSend = send;}

        int getFileFormat() const {return fileformat;}
        ALenum getOpenALFormat() const {return openALformat;}
        std::string getFormatString() const {return format_string;}
        std::string getSubFormatString() const {return subformat_string;}

        int getNumSources() {return sources.size();}
        bool getIsStereo() {return nonSpatialisedStereo;}

        static ofEvent<OpenALSoundPlayer *> playbackEnded;
        OpenALSoundPlayer* playerPtr;

	protected:
		void threadedFunction();

	private:
		friend void ofOpenALSoundUpdate();
		void update(ofEventArgs & args);
		void initFFT(int bands);
		float * getCurrentBufferSum(int size);

		static void createWindow(int size);
		static void runWindow(std::vector<float> & signal);
		static void initSystemFFT(int bands);

        bool sfReadFile(const std::filesystem::path& path);
        bool sfStream(const std::filesystem::path& path);
#ifdef OF_USING_MPG123
        bool mpg123ReadFile(const std::filesystem::path& path);
        bool mpg123Stream(const std::filesystem::path& path);
#endif

        size_t readFile(const std::filesystem::path& fileName);
        size_t stream(const std::filesystem::path& fileName);

		bool isStreaming;
		bool bMultiPlay;
		bool bLoop;
		bool bLoadedOk;
		bool bPaused;
		float pan; // 0 - 1
		float volume; // 0 - 1
		float internalFreq; // 44100 ?
		float speed; // -n to n, 1 = normal, -1 backwards
		unsigned int length; // in samples;

		static std::vector<float> window;
		static float windowSum;

		int channels;
		float duration; //in secs
		int samplerate;
		std::vector<ALuint> buffers;
		std::vector<ALuint> sources;

		// fft structures
		std::vector<std::vector<float> > fftBuffers;
		kiss_fftr_cfg fftCfg;
		std::vector<float> windowedSignal;
		std::vector<float> bins;
		std::vector<kiss_fft_cpx> cx_out;


		static kiss_fftr_cfg systemFftCfg;
		static std::vector<float> systemWindowedSignal;
		static std::vector<float> systemBins;
		static std::vector<kiss_fft_cpx> systemCx_out;

		SNDFILE* streamf;
        ALint byteblockalign = 0;
        ALint splblockalign = 0;
		size_t stream_samples_read;
#ifdef OF_USING_MPG123
		mpg123_handle * mp3streamf;
		int stream_encoding;
#endif
		int mp3_buffer_size;
        int fileformat;
        std::string format_string;
        std::string subformat_string;
        ALenum openALformat;
        //int stream_subformat;
        enum FormatType sample_format = Int16;
		double stream_scale;
        std::vector<short> buffer_short;
        std::vector<float> buffer_float;

        std::atomic<bool> stream_end;

        bool nonSpatialisedStereo;

        // OpenAL filter
        ALuint filter;
        float reverbSend;
        bool bUseFilter;
};
