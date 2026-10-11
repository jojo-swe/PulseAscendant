#pragma once
#include <array>
#include <string>
namespace remix {
struct TimelineFrame {
 int act=1,scene=0,chapter=0,shot=0;
 double actSeconds=0,localSeconds=0,motionSeconds=0;
 float transition=1,progress=0,drive=0,bass=0,hit=0,energy=0,cut=0;
 std::array<float,24> spectrum{};
 const char* title="";
};
TimelineFrame sampleTimeline(double seconds);
double sceneStart(int scene);
double showDuration();
double actStart(int act);
double actDuration(int act);
const char* showMusic();
std::string showInfoJson();
double wrapShowTime(double seconds);
int chapterCount();
double chapterStart(int chapter);
}
