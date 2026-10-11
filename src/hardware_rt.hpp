#pragma once
#include "timeline.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <cstdint>
namespace remix {
inline bool rtViewportFits(uint32_t width,uint32_t height,uint64_t maxBytes){return width>0&&height>0&&uint64_t(width)*height<=maxBytes/(4*sizeof(float));}
class HardwareRT {
public:
 HardwareRT();
 ~HardwareRT();
 bool initialize(const std::filesystem::path& shader,bool validation,std::string& reason);
 const float* render(const TimelineFrame&,int width,int height);
 std::string diagnostics() const;
private:
 struct Impl;
 std::unique_ptr<Impl> p_;
};
}
