#include "app.hpp"
#include <cmath>
#include <stdexcept>
namespace remix {
AppOptions parseOptions(const std::vector<std::string>& args) {
 AppOptions o;
 for(size_t i=0;i<args.size();++i) {
  const auto& a=args[i];
  auto value=[&](){if(i+1>=args.size())throw std::runtime_error("Missing value for "+a);return args[++i];};
  auto number=[&](){auto s=value();size_t p=0;double n=std::stod(s,&p);if(p!=s.size()||!std::isfinite(n)||n<0)throw std::runtime_error("Invalid numeric option "+a);return n;};
  if(a=="--retro")o.retro=true;else if(a=="--no-audio")o.noAudio=true;
  else if(a=="--smoke-test")o.smoke=true;else if(a=="--self-test")o.selfTest=true;
  else if(a=="--tour-test")o.tour=true;
  else if(a=="--render-stream")o.renderStream=true;
  else if(a=="--hidden")o.hidden=true;else if(a=="--help"||a=="-h")o.help=true;
  else if(a=="--capture-dir")o.captureDir=value();else if(a=="--assets")o.assets=value();
  else if(a=="--time")o.time=number();else if(a=="--duration")o.duration=number();
  else if(a=="--fps"){double n=number();if(n<1||n>120||std::floor(n)!=n)throw std::runtime_error("FPS must be an integer from 1 to 120");o.fps=int(n);}
  else if(a=="--scene") {double n=number();if(n<1||n>8||std::floor(n)!=n)throw std::runtime_error("Scene must be from 1 to 8");o.scene=int(n)-1;}
  else if(a=="--width"||a=="--height") {double n=number();if(n<64||n>8192||std::floor(n)!=n)throw std::runtime_error("Dimensions must be integers from 64 to 8192");(a=="--width"?o.width:o.height)=int(n);}
  else throw std::runtime_error("Unknown option: "+a);
 }
 if(o.renderStream&&(o.smoke||o.tour||o.selfTest))throw std::runtime_error("Render stream cannot be combined with diagnostic capture modes");
 return o;
}
std::filesystem::path findAssets(const std::filesystem::path& base) {return base/"assets";}
}
