#include <sstream>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <filesystem>
#include <set>
#include <map>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <random>
#include <fstream>
#if defined(__linux__)
#include <sys/resource.h>
#endif
#include <sndfile.h>
#include "OpenALSoundPlayer.h"
using namespace std::chrono;
// Engine tests on a real OpenAL device. Run with ALSOFT_DRIVERS=null for OpenAL Soft's silent
// device (no sound card needed). Skips (exit 77) when no device opens.
static int fails = 0;
#define CHECK(c, ...) do { if(!(c)) { ++fails; std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)
static std::string tmpPath(const char* name) { return (std::filesystem::temp_directory_path() / name).string(); }
static void writeWav(const char* path, double secs, int ch, int fmt) {
  SF_INFO info{}; info.samplerate=44100; info.channels=ch; info.format=SF_FORMAT_WAV|fmt;
  SNDFILE* f=sf_open(path,SFM_WRITE,&info); std::vector<float> buf(size_t(secs*44100)*ch);
  for(size_t i=0;i<buf.size()/ch;++i) for(int c=0;c<ch;++c) buf[i*ch+c]=0.5f*std::sin(2*3.14159265358979*440*i/44100.0);
  sf_writef_float(f,buf.data(),buf.size()/ch); sf_close(f); }
static std::atomic<int> endedCount{0};
static void tickFor(int ms) { auto t0=steady_clock::now(); while(duration_cast<milliseconds>(steady_clock::now()-t0).count()<ms){ OpenALSoundPlayer::updateAll(); std::this_thread::sleep_for(milliseconds(33)); } }
#if defined(__linux__)
static int threads() { std::ifstream f("/proc/self/status"); std::string l; while(std::getline(f,l)) if(l.rfind("Threads:",0)==0) return std::stoi(l.substr(8)); return -1; }
static double cpuSec() { rusage r; getrusage(RUSAGE_SELF,&r); return r.ru_utime.tv_sec+r.ru_stime.tv_sec+(r.ru_utime.tv_usec+r.ru_stime.tv_usec)/1e6; }
#else
static int threads() { return -1; }
static double cpuSec() { return 0.0; }
#endif
static bool waitEnd(OpenALSoundPlayer& p, int maxMs) { auto t0=steady_clock::now(); while(!p.hasEnded()) { OpenALSoundPlayer::updateAll(); std::this_thread::sleep_for(milliseconds(10)); if(duration_cast<milliseconds>(steady_clock::now()-t0).count()>maxMs) return false; } return true; }
int main(int argc, char** argv) {
  const bool quick = argc>1;
  writeWav(tmpPath("t2s.wav").c_str(), 2.0, 2, SF_FORMAT_PCM_16);
  writeWav(tmpPath("t1sf.wav").c_str(), 1.0, 2, SF_FORMAT_FLOAT);
  writeWav(tmpPath("t05.wav").c_str(), 0.5, 1, SF_FORMAT_PCM_16);
  AudioDevice::initialize();
  OpenALSoundPlayer::addPlaybackEndedListener(&endedCount, [](OpenALSoundPlayer*){ endedCount++; });
  for (int stream=1; stream>=0; --stream) {
    std::printf("T1 play to end (%s)\n", stream?"streamed":"memory");
    OpenALSoundPlayer p; CHECK(p.load(tmpPath("t2s.wav").c_str(), stream), "load");
    int before=endedCount; auto t0=steady_clock::now(); p.play(); float last=0; bool mono=true;
    while(!p.hasEnded() && duration_cast<milliseconds>(steady_clock::now()-t0).count()<4000){ OpenALSoundPlayer::updateAll(); float pos=p.getPosition(); if(pos+1e-4<last) mono=false; last=pos; std::this_thread::sleep_for(milliseconds(20)); }
    double el=duration_cast<milliseconds>(steady_clock::now()-t0).count()/1000.0;
    CHECK(p.hasEnded(), "never ended"); CHECK(endedCount==before+1, "ended notices %d", endedCount-before);
    CHECK(el>1.5 && el<2.3, "rendered end after %.2fs", el); CHECK(mono, "position went backwards");
    CHECK(p.isPlaying(), "should still be playing the tail right after the end");
    tickFor(500); CHECK(!p.isPlaying(), "tail never finished");
  }
  { std::printf("T2 float file, pause/resume, seek\n");
    OpenALSoundPlayer p; CHECK(p.load(tmpPath("t1sf.wav").c_str(), true), "load float");
    p.play(); tickFor(200); p.setPaused(true); float a=p.getPosition(); tickFor(300); float b=p.getPosition();
    CHECK(std::fabs(a-b)<0.02, "moved while paused %.3f %.3f", a, b); CHECK(p.isPaused(), "not paused");
    p.setPaused(false); tickFor(150); CHECK(p.getPosition()>b, "didn't resume");
    p.seekTo(0.1f); tickFor(100); float c=p.getPosition(); CHECK(c>0.08 && c<0.35, "seek landed at %.3f", c);
    CHECK(waitEnd(p, 2000), "no end after seek"); }
  { std::printf("T3 loop with crossfade keeps going\n");
    OpenALSoundPlayer p; LoopRegion r; r.loop=true; r.start=0.1; r.end=0.4; r.crossfadeMs=50; p.setLoopRegion(r);
    CHECK(p.load(tmpPath("t05.wav").c_str(), true), "load");
    int before=endedCount; p.play(); tickFor(1500); CHECK(p.isPlaying(), "loop stopped"); CHECK(endedCount==before, "loop reported an end");
    float pos=p.getPosition(); CHECK(pos>0.15 && pos<0.85, "loop playhead %.3f", pos);
    r.loop=false; p.setLoopRegion(r); CHECK(waitEnd(p, 2000), "didn't end after loop off"); }
  { std::printf("T4 many voices: threads and CPU\n");
    std::vector<std::unique_ptr<OpenALSoundPlayer>> ps; LoopRegion r; r.loop=true;
    for(int i=0;i<100;++i){ auto p=std::make_unique<OpenALSoundPlayer>(); p->setLoopRegion(r); CHECK(p->load(tmpPath("t2s.wav").c_str(), true),"load %d",i); ps.push_back(std::move(p)); }
    int t0=threads(); for(auto& p:ps) p->play(); std::this_thread::sleep_for(milliseconds(100));
    int t1=threads(); double c0=cpuSec(); auto w0=steady_clock::now(); tickFor(quick?1000:3000);
    double cpu=cpuSec()-c0, wall=duration_cast<milliseconds>(steady_clock::now()-w0).count()/1000.0;
    int playing=0; for(auto& p:ps) playing+=p->isPlaying(); CHECK(playing==100, "only %d of 100 playing", playing);
    std::printf("  threads idle %d, with 100 playing %d; CPU %.0f%% of one core\n", t0, t1, 100*cpu/wall);
    for(auto& p:ps) p->stop(); }
  { std::printf("T5 delete players while playing (x300)\n");
    std::mt19937 rng(1); std::vector<std::unique_ptr<OpenALSoundPlayer>> ps;
    for(int round=0; round<300; ++round){ auto p=std::make_unique<OpenALSoundPlayer>(); p->load(tmpPath("t05.wav").c_str(), round%2); p->play(); ps.push_back(std::move(p));
      if(ps.size()>20){ ps.erase(ps.begin()+rng()%ps.size()); } if(round%10==0) OpenALSoundPlayer::updateAll(); std::this_thread::sleep_for(microseconds(rng()%3000)); }
    ps.clear(); std::printf("  survived\n"); }
  { std::printf("T6 voice budget\n");
    std::vector<std::unique_ptr<OpenALSoundPlayer>> ps; const int base=OpenALSoundPlayer::voiceCount();
    for(int i=0;i<700;++i){ auto p=std::make_unique<OpenALSoundPlayer>(); CHECK(p->load(tmpPath("t05.wav").c_str(), true),"load %d",i); ps.push_back(std::move(p)); }
    CHECK(OpenALSoundPlayer::voiceCount()==512, "voices after loading 700: %d", OpenALSoundPlayer::voiceCount());
    auto& lazy=*ps[650]; CHECK(!lazy.hasVoice(), "650 has a voice"); CHECK(lazy.getPosition()==0.0f, "lazy position");
    int before=endedCount; lazy.play(); CHECK(!!lazy.hasVoice(), "no voice on play");
    CHECK(waitEnd(lazy, 1500), "lazy voice never ended"); CHECK(endedCount==before+1, "lazy end notices");
    auto& seekLazy=*ps[600]; seekLazy.seekTo(0.5f); CHECK(seekLazy.hasVoice(), "seek didn't make a voice");
    seekLazy.play(); tickFor(100); float sp=seekLazy.getPosition(); CHECK(sp>0.45, "lazy seek then play at %.3f", sp);
    // Exhaustion: try to play everything; at most the OpenAL limit sounds, nothing crashes.
    for(auto& p:ps) { p->play(); }
    tickFor(200);
    int voiced=0;
    for(auto& p:ps) { voiced+=!!p->hasVoice(); }
    std::printf("  playing all 700: %d got voices (OpenAL limit is about 1280 minus earlier tests)\n", voiced);
    ps.clear(); CHECK(OpenALSoundPlayer::voiceCount()==base, "voices leaked: %d vs %d", OpenALSoundPlayer::voiceCount(), base); }
  { std::printf("T7 out of sources: idle voices are freed and it retries\n");
    std::vector<std::unique_ptr<OpenALSoundPlayer>> ps;
    for(int i=0;i<1400;++i){ auto p=std::make_unique<OpenALSoundPlayer>(); p->load(tmpPath("t05.wav").c_str(), false); ps.push_back(std::move(p)); }
    // Give every one a voice by playing and stopping it (idle, but recently used).
    int noVoice=0; for(auto& p:ps){ p->play(); p->stop(); noVoice += !p->hasVoice(); }
    std::printf("  1400 played and stopped: %d voices, %d without\n", OpenALSoundPlayer::voiceCount(), noVoice);
    CHECK(noVoice==0, "%d players got no voice even though idle ones could be freed", noVoice);
    ps.clear(); }
  OpenALSoundPlayer::removePlaybackEndedListener(&endedCount);
  AudioDevice::close();
  std::printf("%s: %d failures\n", fails? "FAILED":"ALL OK", fails); return fails!=0; }
