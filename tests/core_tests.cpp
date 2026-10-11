#include "timeline.hpp"
#include "audio.hpp"
#include "scene_math.hpp"
#include "app.hpp"
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
  CHECK(showDuration()>470 && showDuration()<473);
  CHECK(chapterCount()>=30);
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
  auto bandsBefore=sampleTimeline(100).spectrum,bandsAfter=sampleTimeline(100.05).spectrum,bandsMiddle=sampleTimeline(100.025).spectrum;
  for(size_t i=0;i<24;++i)CHECK(std::abs(bandsMiddle[i]-(bandsBefore[i]+bandsAfter[i])*.5f)<.0001f);
  for(int scene=0;scene<8;++scene)CHECK(sampleTimeline(sceneStart(scene)+.001).scene==scene);
  for(int chapter=0;chapter<chapterCount();++chapter) {
   double start=chapterStart(chapter),end=chapter+1<chapterCount()?chapterStart(chapter+1):showDuration();
   CHECK(end-start>=5 && end-start<=21);
   CHECK(sampleTimeline(start+.001).chapter==chapter);
   CHECK(sampleTimeline(end-.001).chapter==chapter);
  }
  for(double t=0;t<showDuration();t+=.05) {
   auto f=sampleTimeline(t);
   CHECK(f.scene>=0&&f.scene<8&&f.shot>=0&&f.shot<4);
   CHECK(f.drive>=0&&f.drive<=1&&f.hit>=0&&f.hit<=1&&f.bass>=0&&f.bass<=1);
   CHECK(std::isfinite(f.motionSeconds)&&f.progress>=0&&f.progress<=1);
   if(t>2 && t<showDuration()-2)CHECK(f.transition>.99f);
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
  CHECK(double(wav.size())/2/48000>470); // The complete user-provided song must be present.
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
