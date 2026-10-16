#pragma once
#include <array>
#include <string>
namespace remix {
struct TimelineFrame {
 int act=1,scene=0,chapter=0,shot=0,variant=0;
 double actSeconds=0,localSeconds=0,motionSeconds=0,accentSeconds=0,chapterDuration=0;
 float transition=1,progress=0,chapterProgress=0,event=0,action=0,drive=0,bass=0,hit=0,energy=0,cut=0;
 float kick=0,snare=0,treble=0,pulsePhase=0;
 // Tracked musical grid: beat/bar phase and beat-synced camera shots within a chapter.
 float beatPhase=0,barPhase=0,beatSeconds=.5,shotCut=0;
 int beatIndex=0,shotIndex=0;double shotSeconds=0;
 // Camera-only signals, deliberately free of transients: cameraDrive is the
 // drive smoothed over ~1.5 s, shotDrive its chapter mean (constant in a
 // chapter) and flowSeconds a motion clock integrated from smoothed speed, so
 // camera paths, speed dust and passing streams glide instead of surging per kick.
 float cameraDrive=0,shotDrive=0;double flowSeconds=0;
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
// Beat-synced camera cuts inside a chapter (downbeat times, show seconds).
int shotCount(int chapter);
}
