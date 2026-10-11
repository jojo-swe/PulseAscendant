#include "app.hpp"
#include "timeline.hpp"
#include <iostream>
#include <exception>
int main(int argc,char** argv) {
 try {
  std::vector<std::string> args;for(int i=1;i<argc;++i)args.emplace_back(argv[i]);
  auto options=remix::parseOptions(args);
  if(options.showInfo){std::cout<<remix::showInfoJson();return 0;}
  if(options.help) {std::cout<<"PULSE ASCENDANT\nEsc quit | F11 fullscreen | F1/F2/F3 act | 1-8,9,0,-,=,Q,W,E,R world | arrows chapter | Tab retro | M mute\n--scene 1..16 --retro --time SECONDS --duration SECONDS\n--width N --height N --no-audio --smoke-test --tour-test --capture-dir PATH\n--no-rt (OpenGL fallback) --require-rt (strict Vulkan RT) --rt-validation\n--self-test --hidden --assets PATH --show-info (playable acts and chapters as JSON)\n--render-stream --fps N (bottom-up RGB24 to stdout; duration is timeline length)\n";return 0;}
  return remix::runApp(options);
 } catch(const std::exception& e) {std::cerr<<"Pulse Ascendant: "<<e.what()<<"\nUse --help for options.\n";return 1;}
}
