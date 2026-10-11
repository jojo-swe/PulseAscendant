#pragma once
#include "audio.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include <atomic>
namespace remix {
class AudioPlayer {
public:
 ~AudioPlayer();
 bool start(const std::filesystem::path& path);
 void stop();
 void seek(double seconds);
 void setMuted(bool muted);
 double positionSeconds() const;
 bool active() const { return stream_ && !failed_.load(); }
 std::string status() const;
 uint64_t suppliedFrames() const { return supplied_.load(); }
 float level() const {return level_.load();}
 void refreshOutputAvailability();
 void handleOutputAvailability(bool available);
 bool outputLost() const {return outputLost_;}
private:
 static void SDLCALL callback(void*,SDL_AudioStream*,int,int);
 SDL_AudioStream* stream_=nullptr;
 std::unique_ptr<AudioTransport> transport_;
 std::string status_="SILENT MODE";
 std::atomic<bool> failed_{false};
 std::atomic<uint64_t> supplied_{0};
 std::atomic<float> level_{0};
 bool outputLost_=false;
};
}
