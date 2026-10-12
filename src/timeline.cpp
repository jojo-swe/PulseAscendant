#include "timeline.hpp"
#include "music_data.hpp"
#include "music_data_act2.hpp"
#include "music_data_act3.hpp"
#include "act1.hpp"
#include "act2.hpp"
#include "act3.hpp"
#include "cue_data.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
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
struct Direction {int variant=0;double motionStart=0,duration=0,accent=0;};
const auto& directions(){
 // Seeking samples the same integrated motion and one-time stage action.
 static const auto result=[](){
  std::array<Direction,act1::cues.size()+act2::cues.size()+act3::cues.size()> result{};
  std::array<int,16> visits{};double motion=0;
  for(int chapter=0;chapter<chapterCount();++chapter){
   const int act=chapterAct(chapter)+1;const auto& c=cue(chapter);
   const double start=chapterStart(chapter),end=chapter+1<chapterCount()?chapterStart(chapter+1):showDuration();
   auto& d=result[chapter];d.variant=visits[c.scene]++;d.motionStart=motion;d.duration=end-start;d.accent=d.duration*.5;
   float best=-1;const double actOffset=start-actStart(act);
   for(double at=std::ceil(d.duration*.28*20)/20;at<=d.duration*.76;at+=.05){
    const float onset=std::max(0.f,envelope(act,actOffset+at,1)-envelope(act,actOffset+at-.1,1));
    const float score=envelope(act,actOffset+at,2)+onset*.5f+.02f*float(1-std::abs(at/d.duration-.52));
    if(score>best){best=score;d.accent=at;}
   }
   motion+=d.duration*(1.+c.drive*1.2);
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
double sceneStart(int scene){scene=std::clamp(scene,0,15);for(int i=0;i<chapterCount();++i)if(cue(i).scene==scene)return chapterStart(i);return 0;}
std::string showInfoJson(){
 std::ostringstream out;out<<std::setprecision(12);
 out<<"{\"title\":\"PULSE ASCENDANT\",\"version\":\""<<REMIX_VERSION<<"\",\"duration\":"<<showDuration()<<",\"music\":\""<<showMusic()
    <<"\",\"planned_acts\":3,\"act2_available\":true,\"act3_available\":true,\"acts\":[";
 for(int i=0;i<3;++i){if(i)out<<',';out<<"{\"number\":"<<i+1<<",\"start\":"<<actStart(i+1)<<",\"duration\":"<<actDuration(i+1)<<",\"frames\":"<<tracks[i].frames<<",\"title\":\""<<tracks[i].title<<"\",\"music\":\""<<tracks[i].music<<"\"}";}
 out<<"],\"chapters\":[";
 for(int i=0;i<chapterCount();++i){if(i)out<<',';const auto& d=directions()[i];out<<"{\"start\":"<<chapterStart(i)<<",\"duration\":"<<d.duration<<",\"accent\":"<<chapterStart(i)+d.accent<<",\"act\":"<<chapterAct(i)+1<<",\"scene\":"<<cue(i).scene<<",\"variant\":"<<d.variant<<",\"title\":\""<<cue(i).title<<"\"}";}
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
 f.energy=envelope(f.act,f.actSeconds,0);f.bass=envelope(f.act,f.actSeconds,1);f.hit=envelope(f.act,f.actSeconds,2);
 const auto& track=tracks[f.act-1];double at=f.actSeconds*20;int i=std::clamp(int(at),0,track.count-1),j=std::min(i+1,track.count-1);float blend=float(at-std::floor(at));
 for(int band=0;band<24;++band)f.spectrum[band]=(track.spectrum[i*24+band]*(1-blend)+track.spectrum[j*24+band]*blend)/255.f;
 f.drive=std::clamp(c.drive*(.72f+.28f*f.energy)+.1f*f.bass,0.f,1.f);
 f.motionSeconds=direction.motionStart+local*(1.+c.drive*1.2);f.cut=float(std::exp(-local*8.));
 f.transition=float(std::clamp(std::min(f.actSeconds,actDuration(f.act)-f.actSeconds),0.,1.));return f;
}
}
