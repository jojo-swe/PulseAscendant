#include "timeline.hpp"
#include "music_data.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace remix {
namespace {
struct Cue {double start;int scene,variant;float drive;const char* title;};
constexpr std::array<Cue,31> cues={{
 {0,5,0,.35f,"IGNITION"},{12,0,0,.7f,"PHOTON LAUNCH"},{28,3,0,.75f,"NEON DESCENT"},
 {44,6,0,.6f,"QUANTUM OCEAN"},{60,4,0,.85f,"FRACTURE GATE"},{72,0,1,1,"HYPERDRIVE"},
 {88,3,1,1,"SKYLINE CHASE"},{104,2,0,.95f,"REACTOR OVERDRIVE"},{120,5,1,.95f,"ORBITAL STORM"},
 {136,4,1,.9f,"INFINITE MACHINE"},{152,1,1,.8f,"MATTER COLLAPSE"},{168,7,2,1,"ORBITAL ARMADA"},
 {184,0,2,1,"CORE DIVE"},{200,2,1,1,"CRYSTAL ACCELERATOR"},{216,4,2,1,"FRACTURE CASCADE"},
 {232,5,2,1,"SUPERNOVA"},{248,3,3,1,"TERMINAL VELOCITY"},{264,7,3,1,"EVENT HORIZON"},
 {280,1,2,.7f,"AFTERSHOCK"},{294,6,3,.3f,"ZERO GRAVITY"},{308,2,2,.45f,"REBUILD"},
 {324,7,3,.65f,"RESONANCE"},{340,6,3,.65f,"REBIRTH"},{356,3,0,.9f,"SECOND IGNITION"},
 {372,0,1,1,"FULL THROTTLE"},{388,4,0,.95f,"VECTOR STORM"},{404,5,0,.95f,"SOLAR WIND"},
 {420,2,3,1,"CRYSTAL SUPREMACY"},{436,7,1,1,"FINAL PURSUIT"},{452,5,2,1,"LIGHTSPEED"},
 {466,2,0,.85f,"SECOND REALITY"}
}};
float envelope(double t,int channel) {
 double at=t*music_data::rate;int i=std::clamp(int(at),0,music_data::count-1),j=std::min(i+1,music_data::count-1);
 float f=float(at-std::floor(at));
 return (music_data::envelope[i*3+channel]*(1-f)+music_data::envelope[j*3+channel]*f)/255.f;
}
const auto& starts() {
 static const auto result=[] {
  std::array<double,cues.size()> out{};
  for(size_t i=1;i<cues.size();++i) {
   double best=cues[i].start;float score=-1;
   // Snap editorial cuts to measured transients, without inventing a fixed BPM.
   for(int n=-7;n<=7;++n) {double t=cues[i].start+n*.05;float s=envelope(t,2)-float(std::abs(n))*.025f;if(s>score){score=s;best=t;}}
   out[i]=best;
  }
  return out;
 }();
 return result;
}
}
double showDuration(){return music_data::duration;}
double wrapShowTime(double seconds){return std::isfinite(seconds)&&seconds>0?std::fmod(seconds,showDuration()):0;}
int chapterCount(){return int(cues.size());}
double chapterStart(int chapter){return starts()[std::clamp(chapter,0,chapterCount()-1)];}
double sceneStart(int scene){scene=std::clamp(scene,0,7);for(size_t i=0;i<cues.size();++i)if(cues[i].scene==scene)return starts()[i];return 0;}
TimelineFrame sampleTimeline(double seconds) {
 double t=wrapShowTime(seconds);int index=0;
 while(index+1<chapterCount()&&t+1e-9>=starts()[index+1])++index;
 const auto& cue=cues[index];double local=std::max(0.,t-starts()[index]);
 TimelineFrame f;f.scene=cue.scene;f.chapter=index;f.title=cue.title;f.localSeconds=local;
 f.shot=(cue.variant+int(local/4.))%4;f.progress=float(std::fmod(local,4.)/4.);
 f.energy=envelope(t,0);f.bass=envelope(t,1);f.hit=envelope(t,2);
 double at=t*music_data::rate;int i=std::clamp(int(at),0,music_data::count-1),j=std::min(i+1,music_data::count-1);
 float blend=float(at-std::floor(at));
 for(int band=0;band<music_data::bands;++band)
  f.spectrum[band]=(music_data::spectrum[i*music_data::bands+band]*(1-blend)+music_data::spectrum[j*music_data::bands+band]*blend)/255.f;
 f.drive=std::clamp(cue.drive*(.72f+.28f*f.energy)+.1f*f.bass,0.f,1.f);
 f.motionSeconds=t+local*(1.+cue.drive*2.2);
 f.cut=float(std::exp(-local*8.));
 f.transition=float(std::clamp(std::min(t/1.5,(showDuration()-t)/1.5),0.,1.));
 return f;
}
}
