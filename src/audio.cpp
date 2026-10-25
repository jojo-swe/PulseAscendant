#include "audio_player.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
namespace remix {
AudioPlayer::~AudioPlayer() {stop();}
void AudioPlayer::stop() {
 if(stream_) {SDL_DestroyAudioStream(stream_);stream_=nullptr;}
 transport_.reset();
 level_=0;
}
bool AudioPlayer::start(const std::filesystem::path& path,double expectedDuration) {
 stop(); failed_=false; supplied_=0;outputLost_=false;
 try {
  transport_=std::make_unique<AudioTransport>(readWav(path));
  if(expectedDuration>0&&std::abs(transport_->durationSeconds()-expectedDuration)>.5/48000.)
   throw std::runtime_error("Music length differs from the playable show");
 }
 catch(const std::exception& e) {transport_.reset();status_=std::string("SILENT MODE: ")+e.what();return false;}
 SDL_AudioSpec spec{SDL_AUDIO_F32,2,48000};
 stream_=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,callback,this);
 if(!stream_) {status_=std::string("SILENT MODE: ")+SDL_GetError();transport_.reset();return false;}
 if(!SDL_ResumeAudioStreamDevice(stream_)) {status_=SDL_GetError();stop();return false;}
 status_="PULSE ASCENDANT / THREE TRACKS / JOYOLO";return true;
}
void SDLCALL AudioPlayer::callback(void* userdata,SDL_AudioStream* stream,int additional,int) {
 auto& p=*static_cast<AudioPlayer*>(userdata);
 std::array<float,8192> buffer{};
 size_t remaining=(std::max(0,additional)+7)/8;
 while(remaining) {
  size_t n=std::min<size_t>(remaining,buffer.size()/2);
  p.transport_->render(buffer.data(),n);
  float sum=0;for(size_t i=0;i<n*2;++i)sum+=buffer[i]*buffer[i];
  float rms=std::sqrt(sum/float(n*2));
  p.level_=std::clamp(p.level_.load()*.75f+rms*.25f,0.f,1.f);
  if(!SDL_PutAudioStreamData(stream,buffer.data(),int(n*8))) {p.failed_=true;return;}
  p.supplied_+=n;remaining-=n;
 }
}
void AudioPlayer::seek(double s) {
 if(!stream_) return;
 SDL_LockAudioStream(stream_);
 SDL_ClearAudioStream(stream_);transport_->seek(s);
 SDL_UnlockAudioStream(stream_);
}
void AudioPlayer::setMuted(bool muted) {
 if(!stream_) return;
 SDL_LockAudioStream(stream_);transport_->setMuted(muted);SDL_UnlockAudioStream(stream_);
}
double AudioPlayer::positionSeconds() const {
 if(!stream_) return 0;
 SDL_LockAudioStream(stream_);
 double t=transport_->positionSeconds()-std::max(0,SDL_GetAudioStreamQueued(stream_))/8./48000.;
 double duration=transport_->durationSeconds();
 SDL_UnlockAudioStream(stream_);
 // Input queue compensation; hardware/output backend latency is not exposed exactly.
 double wrapped=std::fmod(t,duration);
 return wrapped<0?wrapped+duration:wrapped;
}
std::string AudioPlayer::status() const {return failed_?"SILENT MODE: AUDIO STREAM FAILED":status_;}
void AudioPlayer::handleOutputAvailability(bool available) {
 if(stream_&&!available) {stop();outputLost_=true;status_="SILENT MODE: OUTPUT UNAVAILABLE";}
}
void AudioPlayer::refreshOutputAvailability() {
 // SDL excludes disconnected zombie physical devices from this enumeration.
 // Default streams may migrate without receiving a logical-device removal event.
 int count=0;auto devices=SDL_GetAudioPlaybackDevices(&count);
 handleOutputAvailability(devices&&count>0);SDL_free(devices);
}
}
