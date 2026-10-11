#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace remix {
struct AppOptions {
 int width=1280,height=720,scene=-1,fps=60;
 double time=0,duration=0;
 bool retro=false,noAudio=false,smoke=false,tour=false,selfTest=false,hidden=false,help=false,renderStream=false;
 std::filesystem::path captureDir,assets;
};
AppOptions parseOptions(const std::vector<std::string>& args);
std::filesystem::path findAssets(const std::filesystem::path& executableDirectory);
int runApp(const AppOptions&);
}
