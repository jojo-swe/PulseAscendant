#include "app.hpp"
#include <iostream>
#include <exception>
int main(int argc,char** argv) {
 try {
  std::vector<std::string> args;for(int i=1;i<argc;++i)args.emplace_back(argv[i]);
  auto options=remix::parseOptions(args);
  if(options.help) {std::cout<<"Second Reality / Full Throttle\nEsc quit | F11 fullscreen | 1-8 world | arrows chapter | Tab retro | M mute\n--scene 1..8 --retro --time SECONDS --duration SECONDS\n--width N --height N --no-audio --smoke-test --tour-test --capture-dir PATH\n--self-test --hidden --assets PATH\n--render-stream --fps N (bottom-up RGB24 to stdout; duration is timeline length)\n";return 0;}
  return remix::runApp(options);
 } catch(const std::exception& e) {std::cerr<<"Second Reality: "<<e.what()<<"\nUse --help for options.\n";return 1;}
}
