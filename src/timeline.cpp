#include "timeline.hpp"
#include "music_data.hpp"
#include "music_data_act2.hpp"
#include "music_data_act3.hpp"
#include "act1.hpp"
#include "act2.hpp"
#include "act3.hpp"
#include "cue_data.hpp"
#include "music_reactivity.hpp"
#include "beat_data.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <vector>
namespace remix {
namespace {
static_assert(music_data::duration==act1::duration,"Regenerate Act I analysis");
static_assert(act1::cues.size()==directed_cues::act1.size()&&act2::cues.size()==directed_cues::act2.size()&&act3::cues.size()==directed_cues::act3.size(),"Regenerate musical direction");
struct Track {const uint8_t* envelope;const uint8_t* spectrum;int count;const act1::Cue* cues;const double* starts;int chapters;std::uint64_t frames;const char* title;const char* music;};
constexpr Track tracks[]={
 {music_data::envelope,music_data::spectrum,music_data::count,act1::cues.data(),directed_cues::act1.data(),int(act1::cues.size()),act1::frames,act1::title,act1::music},
 {music_data_act2::envelope,music_data_act2::spectrum,music_data_act2::count,act2::cues.data(),directed_cues::act2.data(),int(act2::cues.size()),act2::frames,act2::title,act2::music},
 {music_data_act3::envelope,music_data_act3::spectrum,music_data_act3::count,act3::cues.data(),directed_cues::act3.data(),int(act3::cues.size()),act3::frames,act3::title,act3::music}};
int chapterAct(int chapter){int act=0;while(act<2&&chapter>=tracks[act].chapters){chapter-=tracks[act].chapters;++act;}return act;}
int localChapter(int chapter,int act){for(int i=0;i<act;++i)chapter-=tracks[i].chapters;return chapter;}
const act1::Cue& cue(int chapter){int act=chapterAct(chapter);return tracks[act].cues[localChapter(chapter,act)];}
float envelope(int act,double t,int channel){
 const auto& track=tracks[act-1];double at=t*20;int i=std::clamp(int(at),0,track.count-1),j=std::min(i+1,track.count-1);float f=float(at-std::floor(at));
 return (track.envelope[i*3+channel]*(1-f)+track.envelope[j*3+channel]*f)/255.f;
}
float pulse(int act,double t,int channel){
 const int start=reactivity_data::offsets[act-1],count=reactivity_data::offsets[act]-start;
 const double at=std::clamp(t*reactivity_data::rate,0.,double(count-1));
 const int i=int(at),j=std::min(i+1,count-1);const float f=float(at-i);
 return (reactivity_data::values[(start+i)*4+channel]*(1-f)+reactivity_data::values[(start+j)*4+channel]*f)/255.f;
}
double musicalMotion(int act,double t){
 // Integrate velocity, never multiply absolute time by the current loudness.
 // Identical positions on seeking/export; no backward jumps on falling beats.
 static const auto integrals=[](){
  std::array<std::vector<double>,3> out;
  for(int a=1;a<=3;++a){
   const int count=reactivity_data::offsets[a]-reactivity_data::offsets[a-1];
   auto& values=out[a-1];values.resize(count+1);double previous=0;
   for(int i=0;i<=count;++i){
    const double at=double(i)/reactivity_data::rate;
    const double speed=.5*envelope(a,at,0)+.8*pulse(a,at,0)+.35*pulse(a,at,1);
    if(i)values[i]=values[i-1]+(speed+previous)*.5/reactivity_data::rate;
    previous=speed;
   }
  }
  return out;
 }();
 const auto& values=integrals[act-1];const double at=std::clamp(t*reactivity_data::rate,0.,double(values.size()-1));
 const size_t i=size_t(at),j=std::min(i+1,values.size()-1);return values[i]+(values[j]-values[i])*(at-i);
}
struct Beats {const double* times;const unsigned char* bar;int count;};
constexpr Beats beatGrids[]={
 {beat_data::act1_times.data(),beat_data::act1_bar.data(),int(beat_data::act1_times.size())},
 {beat_data::act2_times.data(),beat_data::act2_bar.data(),int(beat_data::act2_times.size())},
 {beat_data::act3_times.data(),beat_data::act3_bar.data(),int(beat_data::act3_times.size())}};
// Index of the last tracked beat at or before act time t (-1 before the first beat).
int beatAt(int act,double t){const auto& g=beatGrids[act-1];return int(std::upper_bound(g.times,g.times+g.count,t+1e-9)-g.times)-1;}
// Bars per camera shot: high-drive chapters cut every two bars, mid-drive every
// four, calm chapters hold a single continuous shot. Cutting on every downbeat
// read as a stutter; a shot now lasts long enough for its glide to register.
// Never cut in a chapter's first bar.
int barsPerShot(float drive){return drive>=.9f?2:(drive>=.55f?4:0);}
// Camera-only series at the reactivity rate. Transient drive and motion speed are
// smoothed with zero-phase exponential passes (two forward, two backward; ~0.7 s
// effective width), so the camera follows phrases and sections, never single
// kicks. Identical values on seeking and export.
struct CameraSeries {std::vector<float> drive;std::vector<double> flow;};
const CameraSeries& cameraSeries(int act){
 static const auto series=[](){
  std::array<CameraSeries,3> out;
  for(int a=1;a<=3;++a){
   const int count=reactivity_data::offsets[a]-reactivity_data::offsets[a-1];
   std::vector<double> drive(count),speed(count);
   for(int i=0;i<count;++i){
    const double at=double(i)/reactivity_data::rate;
    drive[i]=.2*envelope(a,at,0)+.2*pulse(a,at,0)+.1*pulse(a,at,1);
    speed[i]=.5*envelope(a,at,0)+.8*pulse(a,at,0)+.35*pulse(a,at,1);
   }
   const double k=1-std::exp(-1./(.35*reactivity_data::rate));
   for(auto* v:{&drive,&speed})for(int pass=0;pass<2;++pass){
    for(int i=1;i<count;++i)(*v)[i]=(*v)[i-1]+k*((*v)[i]-(*v)[i-1]);
    for(int i=count-2;i>=0;--i)(*v)[i]=(*v)[i+1]+k*((*v)[i]-(*v)[i+1]);
   }
   auto& result=out[a-1];result.drive.resize(count);result.flow.resize(count);
   for(int i=0;i<count;++i)result.drive[i]=float(drive[i]);
   for(int i=1;i<count;++i)result.flow[i]=result.flow[i-1]+(speed[i]+speed[i-1])*.5/reactivity_data::rate;
  }
  return out;
 }();
 return series[std::clamp(act,1,3)-1];
}
template<class T> double sampleSeries(const std::vector<T>& values,double t){
 const double at=std::clamp(t*reactivity_data::rate,0.,double(values.size()-1));
 const size_t i=size_t(at),j=std::min(i+1,values.size()-1);return values[i]+(values[j]-values[i])*(at-double(i));
}
float smoothDrive(int act,double t){return float(sampleSeries(cameraSeries(act).drive,t));}
double smoothMotion(int act,double t){return sampleSeries(cameraSeries(act).flow,t);}
struct Direction {int variant=0;double motionStart=0,flowStart=0,duration=0,accent=0;float shotDrive=0;};
const auto& directions(){
 // Seeking samples the same integrated motion and one-time stage action.
 static const auto result=[](){
  std::array<Direction,act1::cues.size()+act2::cues.size()+act3::cues.size()> result{};
  std::array<int,16> visits{};double motion=0,flow=0;
  for(int chapter=0;chapter<chapterCount();++chapter){
   const int act=chapterAct(chapter)+1;const auto& c=cue(chapter);
   const double start=chapterStart(chapter),end=chapter+1<chapterCount()?chapterStart(chapter+1):showDuration();
   auto& d=result[chapter];d.variant=visits[c.scene]++;d.motionStart=motion;d.flowStart=flow;d.duration=end-start;d.accent=d.duration*.5;
   float best=-1;const double actOffset=start-actStart(act);
   for(double at=std::ceil(d.duration*.28*20)/20;at<=d.duration*.76;at+=.05){
    const float onset=std::max(0.f,envelope(act,actOffset+at,1)-envelope(act,actOffset+at-.1,1));
    const float score=envelope(act,actOffset+at,2)+onset*.5f+.02f*float(1-std::abs(at/d.duration-.52));
    if(score>best){best=score;d.accent=at;}
   }
   motion+=d.duration*(1.3+c.drive*1.65)+musicalMotion(act,actOffset+d.duration)-musicalMotion(act,actOffset);
   flow+=d.duration*(1.3+c.drive*1.65)+smoothMotion(act,actOffset+d.duration)-smoothMotion(act,actOffset);
   // The chapter's mean camera drive sets per-shot rates, which never change mid-shot.
   double mean=0;int samples=0;for(double at=0;at<d.duration;at+=.05,++samples)mean+=smoothDrive(act,actOffset+at);
   d.shotDrive=std::clamp(c.drive*.65f+float(mean/std::max(samples,1)),0.f,1.f);
  }
  return result;
 }();
 return result;
}
}
double actStart(int act){std::uint64_t frames=0;for(int i=0;i<std::clamp(act-1,0,2);++i)frames+=tracks[i].frames;return frames/48000.0;}
double actDuration(int act){return tracks[std::clamp(act-1,0,2)].frames/48000.0;}
double showDuration(){return (act1::frames+act2::frames+act3::frames)/48000.0;}
const char* showMusic(){return "music/second-reality-three-acts.wav";}
double wrapShowTime(double seconds){return std::isfinite(seconds)&&seconds>0?std::fmod(seconds,showDuration()):0;}
int chapterCount(){return int(act1::cues.size()+act2::cues.size()+act3::cues.size());}
double chapterStart(int chapter){chapter=std::clamp(chapter,0,chapterCount()-1);int act=chapterAct(chapter);return actStart(act+1)+tracks[act].starts[localChapter(chapter,act)];}
std::vector<double> shotCuts(int chapter){
 std::vector<double> out;chapter=std::clamp(chapter,0,chapterCount()-1);
 const int act=chapterAct(chapter)+1,every=barsPerShot(cue(chapter).drive);if(!every)return out;
 const double start=chapterStart(chapter)-actStart(act),end=(chapter+1<chapterCount()?chapterStart(chapter+1):showDuration())-actStart(act);
 const auto& g=beatGrids[act-1];int bars=0;
 for(int i=std::max(0,beatAt(act,start)+1);i<g.count&&g.times[i]<end-.75;++i){
  if(g.bar[i]!=0||g.times[i]<start+.9)continue;
  if(++bars%every==0)out.push_back(g.times[i]+actStart(act));
 }
 return out;
}
int shotCount(int chapter){return int(shotCuts(chapter).size())+1;}
double sceneStart(int scene){scene=std::clamp(scene,0,15);for(int i=0;i<chapterCount();++i)if(cue(i).scene==scene)return chapterStart(i);return 0;}
std::string showInfoJson(){
 std::ostringstream out;out<<std::setprecision(12);
 out<<"{\"title\":\"PULSE ASCENDANT\",\"version\":\""<<REMIX_VERSION<<"\",\"duration\":"<<showDuration()<<",\"music\":\""<<showMusic()
    <<"\",\"planned_acts\":3,\"act2_available\":true,\"act3_available\":true,\"acts\":[";
 for(int i=0;i<3;++i){if(i)out<<',';out<<"{\"number\":"<<i+1<<",\"start\":"<<actStart(i+1)<<",\"duration\":"<<actDuration(i+1)<<",\"frames\":"<<tracks[i].frames<<",\"title\":\""<<tracks[i].title<<"\",\"music\":\""<<tracks[i].music<<"\"}";}
 out<<"],\"chapters\":[";
 for(int i=0;i<chapterCount();++i){if(i)out<<',';const auto& d=directions()[i];out<<"{\"start\":"<<chapterStart(i)<<",\"duration\":"<<d.duration<<",\"accent\":"<<chapterStart(i)+d.accent<<",\"act\":"<<chapterAct(i)+1<<",\"scene\":"<<cue(i).scene<<",\"variant\":"<<d.variant<<",\"shots\":"<<shotCount(i)<<",\"title\":\""<<cue(i).title<<"\"}";}
 out<<"]}\n";return out.str();
}
TimelineFrame sampleTimeline(double seconds){
 double t=wrapShowTime(seconds);int index=0;while(index+1<chapterCount()&&t+1e-9>=chapterStart(index+1))++index;
 const auto& c=cue(index);double local=std::max(0.,t-chapterStart(index));
 TimelineFrame f;f.act=chapterAct(index)+1;f.actSeconds=t-actStart(f.act);f.scene=c.scene;f.chapter=index;f.title=c.title;f.localSeconds=local;
 const auto& direction=directions()[index];
 f.accentSeconds=direction.accent;f.chapterDuration=direction.duration;
 f.variant=direction.variant;f.shot=c.variant;
 f.progress=f.chapterProgress=float(std::clamp(local/direction.duration,0.,1.));
 f.event=float(std::exp(-std::abs(local-direction.accent)*5.));
 const float action=float(std::clamp((local-direction.accent+.3)/.6,0.,1.));
 f.action=action*action*(3-2*action);
 f.kick=pulse(f.act,f.actSeconds,0);f.snare=pulse(f.act,f.actSeconds,1);f.treble=pulse(f.act,f.actSeconds,2);f.pulsePhase=pulse(f.act,f.actSeconds,3);
 f.energy=envelope(f.act,f.actSeconds,0);
 f.bass=.28f*envelope(f.act,f.actSeconds,1)+.72f*f.kick;
 f.hit=std::max({envelope(f.act,f.actSeconds,2)*.55f,f.snare,f.kick*.7f,f.treble*.5f});
 const auto& track=tracks[f.act-1];double at=f.actSeconds*20;int i=std::clamp(int(at),0,track.count-1),j=std::min(i+1,track.count-1);float blend=float(at-std::floor(at));
 for(int band=0;band<24;++band)f.spectrum[band]=(track.spectrum[i*24+band]*(1-blend)+track.spectrum[j*24+band]*blend)/255.f;
 f.drive=std::clamp(c.drive*.65f+.2f*f.energy+.2f*f.kick+.1f*f.snare,0.f,1.f);
 const double actOffset=f.actSeconds-local;
 f.motionSeconds=direction.motionStart+local*(1.3+c.drive*1.65)+musicalMotion(f.act,f.actSeconds)-musicalMotion(f.act,actOffset);
 f.flowSeconds=direction.flowStart+local*(1.3+c.drive*1.65)+smoothMotion(f.act,f.actSeconds)-smoothMotion(f.act,actOffset);
 f.cameraDrive=std::clamp(c.drive*.65f+smoothDrive(f.act,f.actSeconds),0.f,1.f);
 f.shotDrive=direction.shotDrive;
 f.cut=float(std::exp(-local*8.));
 {
  const auto& g=beatGrids[f.act-1];const int i=beatAt(f.act,f.actSeconds);
  const double previous=i>=0?g.times[i]:0.,next=i+1<g.count?g.times[i+1]:previous+(i>0?g.times[i]-g.times[i-1]:.5);
  f.beatIndex=std::max(i,0);f.beatSeconds=float(std::max(.2,next-previous));
  f.beatPhase=i>=0?float(std::clamp((f.actSeconds-previous)/(next-previous),0.,1.)):0.f;
  f.barPhase=i>=0?(float(g.bar[i])+f.beatPhase)*.25f:0.f;
  // Cache per chapter: cuts are fixed data and seeking must stay exact.
  static std::vector<std::vector<double>> cuts=[](){std::vector<std::vector<double>> all;for(int k=0;k<chapterCount();++k)all.push_back(shotCuts(k));return all;}();
  const auto& mine=cuts[index];int shot=int(std::upper_bound(mine.begin(),mine.end(),t+1e-9)-mine.begin());
  f.shotIndex=shot;f.shotSeconds=shot?t-mine[shot-1]:local;
  f.shotCut=shot?float(std::exp(-f.shotSeconds*10.)):0.f;
 }
 f.transition=float(std::clamp(std::min(f.actSeconds,actDuration(f.act)-f.actSeconds),0.,1.));return f;
}
}
