#include "audio_player.hpp"
#include "timeline.hpp"
#include <iostream>
#include <stdexcept>
int main() {
 if(!SDL_Init(SDL_INIT_AUDIO)) return 2;
 try {
  remix::AudioPlayer p;
  if(p.start("does-not-exist.wav")||p.active()) throw std::runtime_error("Missing music must fail to silent mode");
  if(!p.start(REMIX_MUSIC)) throw std::runtime_error(p.status());
  SDL_Delay(120);
  if(p.positionSeconds()<0.03||p.positionSeconds()>0.5||p.suppliedFrames()==0) throw std::runtime_error("Audio playback clock did not advance");
  p.seek(60); SDL_Delay(30);
  if(p.positionSeconds()<60||p.positionSeconds()>60.5) throw std::runtime_error("Seek retained stale audio");
  SDL_Delay(80);
  if(p.level()<=0.f||p.level()>1.f) throw std::runtime_error("Music RMS meter invalid");
  p.seek(400);SDL_Delay(30);
  if(p.positionSeconds()<400||p.positionSeconds()>400.5)throw std::runtime_error("Full-song clock wrapped at the old 90 second boundary");
  p.seek(remix::showDuration()-.05);SDL_Delay(120);
  if(p.positionSeconds()>.4)throw std::runtime_error("Full-song end did not loop at the decoded sample count");
  for(int chapter=1;chapter<remix::chapterCount();++chapter){
   p.seek(remix::chapterStart(chapter));
   if(remix::sampleTimeline(p.positionSeconds()).chapter!=chapter)throw std::runtime_error("Seek clock selected the previous chapter");
  }
  p.handleOutputAvailability(true);
  if(!p.active())throw std::runtime_error("Available migration stopped music");
  p.handleOutputAvailability(false);
  if(p.active()||p.status().find("OUTPUT UNAVAILABLE")==std::string::npos)throw std::runtime_error("Lost physical output stayed active");
  if(!p.start(REMIX_MUSIC))throw std::runtime_error(p.status());
  p.setMuted(true); double before=p.positionSeconds(); SDL_Delay(50);
  if(p.positionSeconds()<=before) throw std::runtime_error("Mute paused clock");
  p.stop(); if(p.active()) throw std::runtime_error("Audio did not stop");
  std::cout<<"SDL stream, clock, seek, mute and missing-file checks passed\n";
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}
 SDL_Quit();
}
