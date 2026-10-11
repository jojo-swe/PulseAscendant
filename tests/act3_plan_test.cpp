#include "act3_plan.hpp"
#include <limits>

namespace plan=remix::act3_plan;
static_assert(plan::number==3 && plan::available);
static_assert(plan::music[0]!='\0' && plan::sourceMusic[0]!='\0');
static_assert(plan::decodedFrames==14340480);
static_assert(plan::sampleRate==48000 && plan::channels==2);
static_assert(plan::spectrumBands==24 && plan::analysisRate==20);
static_assert(plan::scenes.size()==4);
static_assert(int(plan::scenes[0].scene)==12 && int(plan::scenes[1].scene)==13);
static_assert(int(plan::scenes[2].scene)==14 && int(plan::scenes[3].scene)==15);
static_assert(*plan::startFrame(14400000,1)==14400001);
static_assert(*plan::startFrame(0,0)==0);
static_assert(*plan::startFrame(std::numeric_limits<std::uint64_t>::max(),0)==
              std::numeric_limits<std::uint64_t>::max());
static_assert(!plan::startFrame(std::numeric_limits<std::uint64_t>::max(),1));

int main() {return 0;}
