#include "timeline.hpp"
#include "audio.hpp"
#include "scene_math.hpp"
#include "app.hpp"
#include "hardware_rt.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <fstream>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); ++checks; } while(0)
int main() {
 int checks=0;
 try {
  using namespace remix;
  CHECK(rtViewportFits(3840,2160,128*1024*1024));
  CHECK(!rtViewportFits(5120,2160,128*1024*1024));
  CHECK(!rtViewportFits(0,2160,128*1024*1024));
  CHECK(showDuration()==888.68);
  CHECK(actDuration(1)==300&&actStart(2)==300&&actDuration(2)==289.92);
  CHECK(chapterCount()==76);
  CHECK(std::string(showMusic())=="music/second-reality-three-acts.wav");
  CHECK(showInfoJson().find("\"act3_available\":true")!=std::string::npos);
  CHECK(sampleTimeline(299.99999).act==1&&sampleTimeline(300).act==2);
  CHECK(sampleTimeline(300).chapter==31&&sampleTimeline(300).actSeconds==0);
  CHECK(actStart(3)==589.92&&actDuration(3)==298.76);
  CHECK(sampleTimeline(actStart(3)-1./48000).act==2);
  CHECK(sampleTimeline(actStart(3)).act==3&&sampleTimeline(actStart(3)).chapter==52);
  CHECK(parseOptions({"--show-info"}).showInfo);
  CHECK(parseOptions({"--pulse-gain","0"}).pulseGain==0);
  CHECK(parseOptions({"--pulse-gain","1.5"}).pulseGain==1.5f);
  bool invalidGain=false;try{parseOptions({"--pulse-gain","2.1"});}catch(const std::runtime_error&){invalidGain=true;}CHECK(invalidGain);
  CHECK(sampleTimeline(0).scene==5);
  CHECK(sampleTimeline(-1).chapter==0);
  CHECK(sampleTimeline(std::numeric_limits<double>::infinity()).chapter==0);
  CHECK(sampleTimeline(showDuration()).chapter==0);
  CHECK(sampleTimeline(showDuration()-.001).chapter==chapterCount()-1);
  CHECK(sampleTimeline(100).chapter!=sampleTimeline(190).chapter);
  CHECK(sampleTimeline(100).spectrum.size()==24);
  float spectrumChange=0,spectrumSeparation=0;
  auto previous=sampleTimeline(0).spectrum;
  for(double t=0;t<showDuration();t+=.25) {
   auto bands=sampleTimeline(t).spectrum;
   for(size_t i=0;i<bands.size();++i) {
    CHECK(std::isfinite(bands[i])&&bands[i]>=0&&bands[i]<=1);
    spectrumChange+=std::abs(bands[i]-previous[i]);
    spectrumSeparation+=std::abs(bands[i]-bands[(i+12)%24]);
   }
   previous=bands;
  }
  CHECK(spectrumChange>100&&spectrumSeparation>100);
  // The three measured transient lanes must breathe independently, and motion
  // must accelerate with music without reversing or depending on seek history.
  for(int act=1;act<=3;++act){
   std::array<int,3> strong{},quiet{};double lowSpeed=100,highSpeed=0;
   for(double t=actStart(act)+1;t<actStart(act)+actDuration(act)-1;t+=.01){
    const auto f=sampleTimeline(t),next=sampleTimeline(t+.001);
    const float channels[]={f.kick,f.snare,f.treble};
    for(int lane=0;lane<3;++lane){CHECK(channels[lane]>=0&&channels[lane]<=1);strong[lane]+=channels[lane]>.6f;quiet[lane]+=channels[lane]<.15f;}
    CHECK(f.pulsePhase>=0&&f.pulsePhase<=1);
    if(f.chapter==next.chapter){double speed=(next.motionSeconds-f.motionSeconds)/.001;lowSpeed=std::min(lowSpeed,speed);highSpeed=std::max(highSpeed,speed);CHECK(speed>1&&speed<5);}
    CHECK(sampleTimeline(t).motionSeconds==f.motionSeconds);
   }
   for(int lane=0;lane<3;++lane)CHECK(strong[lane]>50&&quiet[lane]>50);
   CHECK(highSpeed-lowSpeed>.5);
  }
  // Camera signals must glide: no per-kick steps in the camera drive, a constant
  // per-chapter shot rate, and a flow clock whose speed changes far more slowly
  // than the transient-driven motion clock.
  for(int act=1;act<=3;++act){
   double flowJerk=0,motionJerk=0;int samples=0;
   auto previous=sampleTimeline(actStart(act)+1);double previousFlowSpeed=-1,previousMotionSpeed=-1;
   for(double t=actStart(act)+1+1./60;t<actStart(act)+actDuration(act)-1;t+=1./60){
    const auto f=sampleTimeline(t);
    CHECK(f.cameraDrive>=0&&f.cameraDrive<=1&&f.shotDrive>=0&&f.shotDrive<=1);
    CHECK(sampleTimeline(t).flowSeconds==f.flowSeconds);
    if(f.chapter!=previous.chapter){previous=f;previousFlowSpeed=previousMotionSpeed=-1;continue;}
    CHECK(std::abs(f.cameraDrive-previous.cameraDrive)<.004f);
    CHECK(f.shotDrive==previous.shotDrive);
    const double flowSpeed=(f.flowSeconds-previous.flowSeconds)*60,motionSpeed=(f.motionSeconds-previous.motionSeconds)*60;
    CHECK(flowSpeed>1&&flowSpeed<5);
    if(previousFlowSpeed>0){flowJerk+=std::abs(flowSpeed-previousFlowSpeed);motionJerk+=std::abs(motionSpeed-previousMotionSpeed);++samples;}
    previousFlowSpeed=flowSpeed;previousMotionSpeed=motionSpeed;previous=f;
   }
   CHECK(samples>1000&&flowJerk<motionJerk*.1);
  }
  // Beat-synced shots last at least two bars; calm chapters hold one shot.
  int shots=0;for(int chapter=0;chapter<chapterCount();++chapter)shots+=shotCount(chapter);
  CHECK(shots>chapterCount()&&shots<210);
  auto bandsBefore=sampleTimeline(100).spectrum,bandsAfter=sampleTimeline(100.05).spectrum,bandsMiddle=sampleTimeline(100.025).spectrum;
  for(size_t i=0;i<24;++i)CHECK(std::abs(bandsMiddle[i]-(bandsBefore[i]+bandsAfter[i])*.5f)<.0001f);
  for(int scene=0;scene<16;++scene)CHECK(sampleTimeline(sceneStart(scene)+.001).scene==scene);
  std::array<int,16> visits{};
  for(int chapter=0;chapter<chapterCount();++chapter) {
   double start=chapterStart(chapter),end=chapter+1<chapterCount()?chapterStart(chapter+1):showDuration();
   CHECK(end-start>=5-1e-8 && end-start<=18+1e-8);
   CHECK(sampleTimeline(start+.001).chapter==chapter);
   CHECK(sampleTimeline(end-.001).chapter==chapter);
   auto first=sampleTimeline(start+.001),last=sampleTimeline(end-.001);
   CHECK(first.variant==visits[first.scene]++);
   CHECK(std::abs(first.chapterDuration-(end-start))<1e-9);
   const auto accent=sampleTimeline(start+first.accentSeconds);
   CHECK(accent.chapter==chapter&&accent.event>.9999&&std::abs(accent.action-.5f)<.0001f);
   CHECK(first.action==0&&last.action==1);
   CHECK(first.chapterProgress<.001&&last.chapterProgress>.999);
   auto previousFrame=first;float peakEvent=0;
   for(int sample=1;sample<200;++sample){
    const auto frame=sampleTimeline(start+sample*(end-start)/200);
    CHECK(frame.shot==first.shot&&frame.variant==first.variant);
    CHECK(frame.chapterProgress>previousFrame.chapterProgress&&frame.action>=previousFrame.action);
    CHECK(frame.motionSeconds>previousFrame.motionSeconds);
    peakEvent=std::max(peakEvent,frame.event);previousFrame=frame;
   }
   CHECK(peakEvent>.75f);
   if(chapter>0){
    const double before=sampleTimeline(start-.0001).motionSeconds,after=sampleTimeline(start+.0001).motionSeconds;
    CHECK(after>before&&after-before<.001); // no rewind at chapter or act cuts
   }
  }
  for(double t=0;t<showDuration();t+=.05) {
   auto f=sampleTimeline(t);
   CHECK(f.scene>=0&&f.scene<16&&f.shot>=0&&f.shot<4);
   CHECK(f.drive>=0&&f.drive<=1&&f.hit>=0&&f.hit<=1&&f.bass>=0&&f.bass<=1);
   CHECK(std::isfinite(f.motionSeconds)&&f.progress>=0&&f.progress<=1);
   if(f.actSeconds>2 && f.actSeconds<actDuration(f.act)-2)CHECK(f.transition>.99f);
  }
  AudioTransport transport({1,2,3,4,5,6},1);
  float out[8]{};
  transport.seek(2); transport.render(out,4);
  CHECK(out[0]==5 && out[2]==1 && out[4]==3 && out[6]==5);
  CHECK(transport.positionSeconds()==0);
  transport.setMuted(true); transport.render(out,2);
  CHECK(out[0]==0 && out[3]==0);
  CHECK(transport.positionSeconds()==2);
  AudioTransport full(std::vector<float>(4320000*2,0.1f));
  full.seek(sceneStart(0)); CHECK(sampleTimeline(full.positionSeconds()).scene==0);
  full.seek(60); CHECK(full.positionSeconds()==60);
  full.seek(89.9999791667); full.render(out,2);
  CHECK(full.positionSeconds()<0.001);
  auto bad=std::filesystem::temp_directory_path()/"remix-invalid.wav";
  { std::ofstream f(bad); f<<"RIFFbroken"; }
  bool rejected=false; try { readWav(bad); } catch(const std::exception&) { rejected=true; }
  std::filesystem::remove(bad); CHECK(rejected);
  auto wav=readWav(REMIX_MUSIC);
  CHECK(wav.size()==14400000*2); // Act I cannot play any sample after 5:00.
  AudioTransport actAudio(std::move(wav));
  actAudio.seek(299.9999791667);actAudio.render(out,2);
  CHECK(std::abs(out[0])<.001f&&std::abs(out[1])<.001f);
  CHECK(actAudio.positionSeconds()<.001);
  wav=readWav(REMIX_MUSIC);
  float peak=0; for(float v:wav) { CHECK(std::isfinite(v)); peak=std::max(peak,std::abs(v)); }
  CHECK(peak>0.01f && peak<=1.f);
  for(double t:{0.,30.,90.}) {
   auto particles=makeTunnel(t); CHECK(!particles.empty());
   for(auto p:particles) CHECK(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z));
  }
  auto mesh=glenzMesh(); CHECK(mesh.vertices.size()==14);
  for(auto i:mesh.indices) CHECK(i<mesh.vertices.size());
  CHECK(mesh.vertices[0].x==-1 && mesh.vertices[6].x==1);
  CHECK(mesh.vertices[8].z==-1.7f && mesh.vertices[9].z==1.7f);
  auto planes=glenzPlanes();CHECK(planes.size()==24);
  for(auto p:planes) {
   CHECK(std::abs(p.x*p.x+p.y*p.y+p.z*p.z-1.f)<.0001f);
   for(auto v:mesh.vertices)CHECK(p.x*v.x+p.y*v.y+p.z*v.z<=p.w+.0001f);
  }
  auto extent=renderExtent(0,0,false); CHECK(extent.width>=1&&extent.height>=1);
  extent=renderExtent(1920,1080,true); CHECK(extent.width==320&&extent.height==200);
  for(auto args:std::vector<std::vector<std::string>>{{"--scene","0"},{"--width","0"},{"--time","nan"},{"--duration","-2"},{"--width","1280x"},{"--surprise"}}) {
   bool invalid=false;try {parseOptions(args);}catch(const std::exception&){invalid=true;} CHECK(invalid);
  }
  auto options=parseOptions({"--scene","3","--retro","--width","1920"});CHECK(options.scene==2&&options.retro&&options.width==1920);
  CHECK(parseOptions({"--tour-test"}).tour);
  auto exportOptions=parseOptions({"--render-stream","--fps","60","--width","3840","--height","2160"});
  CHECK(exportOptions.renderStream&&exportOptions.fps==60&&exportOptions.width==3840&&exportOptions.height==2160);
  for(auto fps:{"0","121","59.5"}) {
   bool invalid=false;try{parseOptions({"--fps",fps});}catch(const std::exception&){invalid=true;}CHECK(invalid);
  }
  auto base=std::filesystem::temp_directory_path()/"remix-location-test";
  std::filesystem::create_directories(base/"assets");
  CHECK(findAssets(base)==base/"assets");std::filesystem::remove(base/"assets");std::filesystem::remove(base);
  std::cout << checks << " checks passed\n";
 } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
