#include "app.hpp"
#include "audio_player.hpp"
#include "renderer.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <iostream>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <cstdio>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
namespace remix {
int runApp(const AppOptions& o) {
 if(!SDL_Init(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());
 struct Quit {~Quit(){SDL_Quit();}} quit;
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
 SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
 SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
 SDL_Window* raw=SDL_CreateWindow("PULSE ASCENDANT",o.width,o.height,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|(o.smoke||o.tour||o.renderStream||(o.hidden&&!o.selfTest)?SDL_WINDOW_HIDDEN:0));
 if(!raw)throw std::runtime_error(std::string("Window: ")+SDL_GetError());
 std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(raw,SDL_DestroyWindow);
 SDL_GLContext context=SDL_GL_CreateContext(raw);
 if(!context)throw std::runtime_error(std::string("OpenGL 3.3 required: ")+SDL_GetError());
 struct Context {SDL_GLContext c;~Context(){SDL_GL_DestroyContext(c);}} owned{context};
 bool vsync=SDL_GL_SetSwapInterval(o.duration>0||o.smoke||o.tour||o.renderStream?0:1);
 auto assets=o.assets.empty()?findAssets(SDL_GetBasePath()):o.assets;
 if(auto* icon=SDL_LoadBMP((assets/"branding"/"pulse-ascendant.bmp").string().c_str())){SDL_SetWindowIcon(raw,icon);SDL_DestroySurface(icon);}
 Renderer renderer(assets);renderer.initialize(!o.noRT,o.requireRT,o.rtValidation);renderer.resize(o.width,o.height);
 renderer.setPulseGain(o.pulseGain);renderer.setRenderScale(o.renderScale);renderer.setLetterbox(!o.openMatte);
 // Exported films carry no interactive keyboard hints.
 renderer.setControlHints(!o.renderStream);
 (o.renderStream?std::cerr:std::cout)<<"Video: "<<SDL_GetCurrentVideoDriver()<<" | GPU: "<<renderer.diagnostics()<<'\n';
 if(o.renderStream) {
#ifdef _WIN32
  if(_setmode(_fileno(stdout),_O_BINARY)==-1)throw std::runtime_error("Cannot open binary video stream");
#endif
  double initial=o.scene>=0?sceneStart(o.scene):o.time,length=o.duration>0?o.duration:showDuration()-initial;
  if(length<=0||initial+length>showDuration()+1e-9)throw std::runtime_error("Export must stay inside the playable show");
  auto frames=static_cast<unsigned long long>(std::ceil(length*o.fps-1e-9));
  std::vector<unsigned char> pixels;
  for(unsigned long long i=0;i<frames;++i) {
   SDL_Event event;while(SDL_PollEvent(&event))if(event.type==SDL_EVENT_QUIT)throw std::runtime_error("Video export interrupted");
   double t=initial+double(i)/o.fps;
   renderer.render(sampleTimeline(t),t,o.retro);renderer.readPixelsRGB(pixels);
   if(std::fwrite(pixels.data(),1,pixels.size(),stdout)!=pixels.size())throw std::runtime_error("Video encoder pipe closed");
   if(i%(o.fps*5)==0)std::cerr<<"RENDER "<<i<<"/"<<frames<<" time="<<t<<'\n';
  }
  if(std::fflush(stdout)!=0)throw std::runtime_error("Video stream flush failed");
  std::cerr<<"RENDER PASS: "<<frames<<" RGB24 frames, "<<o.width<<"x"<<o.height<<" at "<<o.fps<<" fps\n";
  std::cerr<<"Render backend: "<<renderer.diagnostics()<<'\n';
  return 0;
 }
 if(o.smoke||o.tour) {
  auto dir=o.captureDir.empty()?std::filesystem::path("captures"):o.captureDir;
  if(o.tour){
   for(int chapter=0;chapter<chapterCount();++chapter)for(int sample=0;sample<2;++sample){
    const double start=chapterStart(chapter),end=chapter+1<chapterCount()?chapterStart(chapter+1):showDuration();
    double t=start+(end-start)*(sample?.82:.18);renderer.render(sampleTimeline(t),t,o.retro);
    renderer.capture(dir/("chapter-"+std::to_string(chapter+1)+(sample?"-b.bmp":"-a.bmp")));SDL_GL_SwapWindow(raw);
   }
   std::cout<<"TOUR PASS: "<<chapterCount()*2<<" exact-time chapter captures rendered without GL errors\n";
   std::cout<<"Render backend: "<<renderer.diagnostics()<<'\n';return 0;
  }
  for(int scene=0;scene<16;++scene)for(bool retro:{false,true}) {
   double t=sceneStart(scene)+2.;renderer.render(sampleTimeline(t),t,retro);
   renderer.capture(dir/("scene-"+std::to_string(scene+1)+(retro?"-retro.bmp":"-modern.bmp")));
   SDL_GL_SwapWindow(raw);
  }
  std::cout<<"SMOKE PASS: thirty-two scenes/styles rendered without GL errors\n";
  std::cout<<"Render backend: "<<renderer.diagnostics()<<'\n';return 0;
 }
 double initial=o.scene>=0?sceneStart(o.scene):o.time;
 // Some drivers defer pipeline compilation until the first actual draw. Pay
 // that cost before starting either clock, including the later worlds.
 // Export and capture modes above keep their exact frame counts unchanged.
 if(!o.selfTest) {
  const auto prepareBegin=SDL_GetTicksNS();
  glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);SDL_GL_SwapWindow(raw);
  renderer.resize(64,64);
  auto prepareCancelled=[](){SDL_Event event;while(SDL_PollEvent(&event))if(event.type==SDL_EVENT_QUIT||(event.type==SDL_EVENT_KEY_DOWN&&event.key.key==SDLK_ESCAPE))return true;return false;};
  for(int scene=0;scene<16;++scene) {
   SDL_SetWindowTitle(raw,("PULSE ASCENDANT / Preparing "+std::to_string(scene+1)+" of 16").c_str());
   if(prepareCancelled())return 0;
   double t=sceneStart(scene)+2.;
   // The late corridor also uses the tunnel-point and flight-line programs.
   if(scene==0)for(int chapter=0;chapter<chapterCount();++chapter){const double at=chapterStart(chapter)+2.;auto frame=sampleTimeline(at);if(frame.scene==0&&frame.variant>=3){t=at;break;}}
   renderer.render(sampleTimeline(t),t,o.retro);glFinish();
   // Preload the extra triangle pipeline before an eventual Tab style switch.
   if(scene==2&&!o.retro){renderer.render(sampleTimeline(t),t,true);glFinish();}
  }
  if(prepareCancelled())return 0;
  int preparedWidth=0,preparedHeight=0;SDL_GetWindowSizeInPixels(raw,&preparedWidth,&preparedHeight);
  renderer.resize(preparedWidth,preparedHeight);
  renderer.render(sampleTimeline(initial),initial,o.retro);glFinish();SDL_GL_SwapWindow(raw);
  if(prepareCancelled())return 0;
  SDL_SetWindowTitle(raw,"PULSE ASCENDANT");
  std::cerr<<"PREPARE PASS: all world pipelines and initial viewport ready before audio, "<<(SDL_GetTicksNS()-prepareBegin)/1e9<<" s\n";
 }
 AudioPlayer audio;
 if(!o.noAudio&&SDL_InitSubSystem(SDL_INIT_AUDIO))audio.start(assets/showMusic(),showDuration());
 std::cout<<"Audio: "<<audio.status()<<'\n';
 audio.seek(initial);
 auto now=[](){return SDL_GetTicksNS()/1e9;};
 auto awaitWindow=[&](auto ready,const char* error) {
  SDL_SyncWindow(raw);double deadline=now()+2.;
  while(!ready()){SDL_PumpEvents();if(now()>=deadline)throw std::runtime_error(error);SDL_Delay(10);}
 };
 double begin=now(),fallbackBegin=begin,fallbackTime=initial,lastTime=initial;
 bool retro=o.retro,muted=false,fullscreen=false,running=true,usingAudio=audio.active();
 bool controlsComplete=false;int controlStage=0;double minimizeStart=0,minimizeClock=0;
 int frames=0;double renderTotal=0,maxRender=0;float maxLevel=0;
 auto seek=[&](int scene){double t=sceneStart(scene);audio.seek(t);fallbackTime=t;fallbackBegin=now();lastTime=t;};
 auto key=[&](SDL_Keycode k) {
  if(k==SDLK_ESCAPE)running=false;
  else if(k>=SDLK_1&&k<=SDLK_8)seek(int(k-SDLK_1));
  else if(k==SDLK_9)seek(8);else if(k==SDLK_0)seek(9);else if(k==SDLK_MINUS)seek(10);else if(k==SDLK_EQUALS)seek(11);
  else if(k==SDLK_Q)seek(12);else if(k==SDLK_W)seek(13);else if(k==SDLK_E)seek(14);else if(k==SDLK_R)seek(15);
  else if(k==SDLK_F1||k==SDLK_F2||k==SDLK_F3){double t=actStart(k==SDLK_F3?3:(k==SDLK_F2?2:1));audio.seek(t);fallbackTime=t;fallbackBegin=now();lastTime=t;}
  else if(k==SDLK_RIGHT||k==SDLK_LEFT){int chapter=sampleTimeline(lastTime).chapter+(k==SDLK_RIGHT?1:-1);double t=chapterStart(chapter);audio.seek(t);fallbackTime=t;fallbackBegin=now();lastTime=t;}
  else if(k==SDLK_TAB)retro=!retro;
  else if(k==SDLK_M){muted=!muted;audio.setMuted(muted);}
  else if(k==SDLK_F11){
   fullscreen=!fullscreen;if(!SDL_SetWindowFullscreen(raw,fullscreen))throw std::runtime_error(SDL_GetError());
   if(o.selfTest)awaitWindow([&](){return bool(SDL_GetWindowFlags(raw)&SDL_WINDOW_FULLSCREEN)==fullscreen;},"Fullscreen state did not change");
  }
 };
 if(o.selfTest) {
  // Exercise the same event handler used by real SDL keyboard events.
  for(SDL_Keycode k:{SDLK_3,SDLK_TAB,SDLK_M,SDLK_1}) {
   SDL_Event ev{};ev.type=SDL_EVENT_KEY_DOWN;ev.key.key=k;SDL_PushEvent(&ev);
  }
 }
 while(running) {
  if(o.duration>0&&now()-begin>=o.duration)break;
  SDL_Event ev;
  while(SDL_PollEvent(&ev)) {
   if(ev.type==SDL_EVENT_QUIT)running=false;
   if(ev.type==SDL_EVENT_KEY_DOWN&&!ev.key.repeat)key(ev.key.key);
   if(ev.type==SDL_EVENT_AUDIO_DEVICE_REMOVED&&!ev.adevice.recording&&usingAudio) {
    audio.refreshOutputAvailability();
    if(!audio.active()){fallbackTime=lastTime;fallbackBegin=now();usingAudio=false;}
   }
   if(ev.type==SDL_EVENT_AUDIO_DEVICE_ADDED&&!ev.adevice.recording&&!usingAudio&&audio.outputLost()&&!o.noAudio) {
    double resume=wrapShowTime(fallbackTime+now()-fallbackBegin);
    if(audio.start(assets/showMusic(),showDuration())){audio.seek(resume);audio.setMuted(muted);usingAudio=true;}
   }
  }
  if(!running)break;
  if(usingAudio&&!audio.active()){fallbackTime=lastTime;fallbackBegin=now();usingAudio=false;}
  double t=usingAudio?audio.positionSeconds():wrapShowTime(fallbackTime+now()-fallbackBegin);lastTime=t;
  if(o.duration>0&&now()-begin>=o.duration)break;
  if(o.selfTest&&controlStage==1) {
   // Fullscreen restoration may restore the previous window geometry asynchronously.
   // Test resizing after both fullscreen transitions have completed.
   if(!SDL_SetWindowSize(raw,960,540))throw std::runtime_error("Resize request failed");
   awaitWindow([&](){int rw=0,rh=0;SDL_GetWindowSize(raw,&rw,&rh);return rw==960&&rh==540;},"Resize state not observed");
   if(!SDL_MinimizeWindow(raw))throw std::runtime_error("Minimize request failed");
   awaitWindow([&](){return bool(SDL_GetWindowFlags(raw)&SDL_WINDOW_MINIMIZED);},"Minimized state not observed");
   minimizeStart=now();minimizeClock=t;controlStage=2;continue;
  }
  if(o.selfTest&&controlStage==2) {
   if(!(SDL_GetWindowFlags(raw)&SDL_WINDOW_MINIMIZED))throw std::runtime_error("Minimized state not observed");
   if(now()-minimizeStart<.09){SDL_Delay(10);continue;}
   if(wrapShowTime(t-minimizeClock+showDuration())<.02)throw std::runtime_error("Clock stopped while minimized");
   if(!SDL_RestoreWindow(raw))throw std::runtime_error("Restore request failed");
   awaitWindow([&](){return !(SDL_GetWindowFlags(raw)&SDL_WINDOW_MINIMIZED);},"Restored state not observed");
   controlStage=3;continue;
  }
  if(o.selfTest&&controlStage==3) {
   int rw=0,rh=0;SDL_GetWindowSizeInPixels(raw,&rw,&rh);
   if((SDL_GetWindowFlags(raw)&SDL_WINDOW_MINIMIZED)||rw<=0||rh<=0)throw std::runtime_error("Restored drawable not observed");
   controlStage=4;
  }
  if(SDL_GetWindowFlags(raw)&SDL_WINDOW_MINIMIZED){SDL_Delay(30);continue;}
  int w=0,h=0;SDL_GetWindowSizeInPixels(raw,&w,&h);if(w<=0||h<=0){SDL_Delay(30);continue;}
  renderer.resize(w,h);renderer.setSilent(!usingAudio);
  maxLevel=std::max(maxLevel,audio.level());
  double start=now();renderer.render(sampleTimeline(t),t,retro);
  if(!o.captureDir.empty()&&frames==5)renderer.capture(o.captureDir/"preview.bmp");
  SDL_GL_SwapWindow(raw);double took=now()-start;renderTotal+=took;maxRender=std::max(maxRender,took);++frames;
  if(o.selfTest&&frames==4) {
   if(!retro||!muted||fullscreen||sampleTimeline(t).scene!=0)throw std::runtime_error("Control event test failed: retro="+std::to_string(retro)+" mute="+std::to_string(muted)+" fullscreen="+std::to_string(fullscreen)+" scene="+std::to_string(sampleTimeline(t).scene)+" clock="+std::to_string(t)+" seek="+std::to_string(sceneStart(0)));
   // Present several frames before testing asynchronous desktop fullscreen transitions.
   for(int i=0;i<2;++i){SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.key=SDLK_F11;SDL_PushEvent(&event);}
   controlStage=1;
  }
  if(o.selfTest&&frames==12&&controlStage==4){controlsComplete=true;key(SDLK_ESCAPE);}
 }
 std::cout<<"Render backend: "<<renderer.diagnostics()<<'\n';
 std::cout<<"Frames: "<<frames<<" | FPS: "<<(frames/std::max(.000001,now()-begin))<<" | mean frame ms: "<<renderTotal*1000/std::max(1,frames)<<" | max ms: "<<maxRender*1000<<" | vsync request accepted: "<<vsync<<'\n';
 std::cout<<"Audio frames supplied: "<<audio.suppliedFrames()<<" | playback seconds: "<<lastTime<<'\n';
 std::cout<<"Maximum observed music RMS: "<<maxLevel<<'\n';
 if(o.selfTest&&!controlsComplete)throw std::runtime_error("SELF-TEST INCOMPLETE: duration or exit interrupted lifecycle checks");
 if(o.selfTest)std::cout<<"CONTROLS PASS: SDL key events, seek, style, mute, observed fullscreen/resize/minimize/restore, advancing minimized clock and exit\n";
 return 0;
}
}
