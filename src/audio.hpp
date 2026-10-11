#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <filesystem>
namespace remix {
std::vector<float> readWav(const std::filesystem::path& path);
class AudioTransport {
public:
    explicit AudioTransport(std::vector<float> stereo, int rate = 48000);
    void seek(double seconds);
    void setMuted(bool muted);
    double positionSeconds() const;
    double durationSeconds() const {return double(samples_.size()/2)/rate_;}
    void render(float* output, size_t frames);
private:
    std::vector<float> samples_;
    size_t frame_ = 0;
    int rate_;
    bool muted_ = false;
};
}
