#pragma once
#include <array>
namespace remix {
struct TimelineFrame {
 int scene=0,chapter=0,shot=0;
 double localSeconds=0,motionSeconds=0;
 float transition=1,progress=0,drive=0,bass=0,hit=0,energy=0,cut=0;
 std::array<float,24> spectrum{};
 const char* title="";
};
TimelineFrame sampleTimeline(double seconds);
double sceneStart(int scene);
double showDuration();
double wrapShowTime(double seconds);
int chapterCount();
double chapterStart(int chapter);
}
