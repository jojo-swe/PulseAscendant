#pragma once
#include "timeline.hpp"
#include "gl_api.hpp"
#include "hardware_rt.hpp"
#include <filesystem>
#include <string>
#include <vector>
namespace remix {
class Renderer {
public:
 explicit Renderer(std::filesystem::path assets):assets_(std::move(assets)){}
 ~Renderer();
 void initialize(bool rtEnabled=true,bool rtRequired=false,bool rtValidation=false);
 void resize(int width,int height);
 void render(const TimelineFrame&,double seconds,bool retro);
 void capture(const std::filesystem::path&);
 void readPixelsRGB(std::vector<unsigned char>&);
 std::string diagnostics() const;
 void setSilent(bool silent) {silent_=silent;}
 void setPulseGain(float gain) {pulseGain_=gain;}
 void setRenderScale(float scale) {renderScale_=scale;}
 void setLetterbox(bool on) {letterbox_=on;}
 void setControlHints(bool on) {hints_=on;}
private:
 struct Target { GLuint fbo=0,texture=0; int w=0,h=0; };
 Target scene_,blur_[2],streak_[2],output_;
 GLuint vao_=0,buffer_=0,meshVao_=0,meshBuffer_=0;
 GLuint background_=0,engine_=0,plasma_=0,particle_=0,glenz_=0,blurProgram_=0,composite_=0,text_=0;
 GLuint city_=0,structure_=0,cosmic_=0,ocean_=0,armada_=0,swarm_=0,flight_=0;
 GLuint spectrum_=0;
 GLuint act2_=0,act3_=0;
 std::unique_ptr<HardwareRT> rt_;
 std::string rtStatus_;
 bool rtRequired_=false;
 TimelineFrame frame_;
 int width_=1280,height_=720,meshCount_=0;
 bool silent_=false;
 float pulseGain_=1,renderScale_=1;
 bool letterbox_=true,hints_=true;
 int uniformW_=0,uniformH_=0;
 std::filesystem::path assets_;
 GLuint program(const std::string& vertex,const std::string& fragment);
 std::string source(const std::string& name);
 void target(Target&,int w,int h,bool nearest);
 void release(Target&);
 void uniforms(GLuint p,double seconds,int scene,bool retro);
 void quad(GLuint p);
 void label(const std::string&,float x,float y,float scale,float r,float g,float b,float opacity=1);
 void emblem(float x,float y,float size,float opacity);
};
}
