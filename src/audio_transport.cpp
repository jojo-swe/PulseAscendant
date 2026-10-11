#include "audio.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <cstring>
namespace remix {
AudioTransport::AudioTransport(std::vector<float> stereo,int rate):samples_(std::move(stereo)),rate_(rate) {
 if(rate<=0||samples_.empty()||samples_.size()%2) throw std::runtime_error("Invalid stereo audio buffer");
}
void AudioTransport::seek(double s) {
 if(!std::isfinite(s)||s<0) s=0;
 const auto frames=samples_.size()/2;
 // Preserve exact sample-aligned cue boundaries despite binary decimal rounding.
 frame_=size_t(std::floor(std::fmod(s,double(frames)/rate_)*rate_+1e-7))%frames;
}
void AudioTransport::setMuted(bool muted) { muted_=muted; }
double AudioTransport::positionSeconds() const { return double(frame_)/rate_; }
void AudioTransport::render(float* out,size_t frames) {
 for(size_t i=0;i<frames;++i) {
  out[2*i]=muted_?0:samples_[2*frame_];
  out[2*i+1]=muted_?0:samples_[2*frame_+1];
  frame_=(frame_+1)%(samples_.size()/2);
 }
}
std::vector<float> readWav(const std::filesystem::path& path) {
 std::ifstream f(path,std::ios::binary|std::ios::ate);
 if(!f) throw std::runtime_error("Music file unavailable: "+path.string());
 auto size=f.tellg();
 if(size<44||size>256*1024*1024) throw std::runtime_error("Invalid WAV size");
 std::vector<unsigned char> b(static_cast<size_t>(size)); f.seekg(0);
 if(!f.read(reinterpret_cast<char*>(b.data()),b.size())) throw std::runtime_error("Truncated WAV");
 auto u16=[&](size_t p) {return unsigned(b.at(p))|(unsigned(b.at(p+1))<<8);};
 auto u32=[&](size_t p) {return uint32_t(u16(p))|(uint32_t(u16(p+2))<<16);};
 if(std::memcmp(b.data(),"RIFF",4)||std::memcmp(b.data()+8,"WAVE",4)||uint64_t(u32(4))+8>b.size()) throw std::runtime_error("Invalid WAV header");
 bool format=false; size_t data=0,length=0;
 for(size_t p=12;p+8<=b.size();) {
  size_t n=u32(p+4); if(n>b.size()-p-8) throw std::runtime_error("Truncated WAV chunk");
  if(!std::memcmp(b.data()+p,"fmt ",4)) {
   if(n<16||u16(p+8)!=1||u16(p+10)!=2||u32(p+12)!=48000||u16(p+20)!=4||u16(p+22)!=16) throw std::runtime_error("Music must be stereo 48kHz PCM16 WAV");
   format=true;
  }
  if(!std::memcmp(b.data()+p,"data",4)) {data=p+8;length=n;}
  p+=8+n+(n%2);
 }
 if(!format||!data||!length||length%4) throw std::runtime_error("Missing WAV samples");
 std::vector<float> result(length/2);
 for(size_t i=0;i<result.size();++i) result[i]=int16_t(u16(data+i*2))/32768.f;
 return result;
}
}
