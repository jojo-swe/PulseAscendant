#include "renderer.hpp"
#include "scene_math.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <map>
#include <iostream>
namespace remix {
std::string Renderer::source(const std::string& name) {
 std::ifstream f(assets_/"shaders"/name); if(!f) throw std::runtime_error("Shader unavailable: "+name);
 std::string s((std::istreambuf_iterator<char>(f)),{});
 const std::string inc="#include \"common.glsl\"";
 if(auto p=s.find(inc);p!=std::string::npos) s.replace(p,inc.size(),source("common.glsl"));
 return s;
}
GLuint Renderer::program(const std::string& vertex,const std::string& fragment) {
 auto compile=[&](GLenum type,const std::string& name) {
  auto s=source(name); const char* text=s.c_str();
  GLuint shader=gl::CreateShader(type);gl::ShaderSource(shader,1,&text,nullptr);gl::CompileShader(shader);
  GLint ok=0;gl::GetShaderiv(shader,GL_COMPILE_STATUS,&ok);
  if(!ok) {char log[4096]{};gl::GetShaderInfoLog(shader,4096,nullptr,log);gl::DeleteShader(shader);throw std::runtime_error(name+": "+log);}
  return shader;
 };
 GLuint vs=compile(GL_VERTEX_SHADER,vertex),fs=0,p=0;
 try {
  fs=compile(GL_FRAGMENT_SHADER,fragment);p=gl::CreateProgram();gl::AttachShader(p,vs);gl::AttachShader(p,fs);gl::LinkProgram(p);
  GLint ok=0;gl::GetProgramiv(p,GL_LINK_STATUS,&ok);
  if(!ok) {char log[4096]{};gl::GetProgramInfoLog(p,4096,nullptr,log);throw std::runtime_error("Shader link: "+std::string(log));}
 } catch(...) {gl::DeleteShader(vs);if(fs)gl::DeleteShader(fs);if(p)gl::DeleteProgram(p);throw;}
 gl::DeleteShader(vs);gl::DeleteShader(fs);return p;
}
void Renderer::initialize(bool rtEnabled,bool rtRequired,bool rtValidation) {
 rtRequired_=rtRequired||rtValidation;
 gl::load();
 background_=program("fullscreen.vert","corridor.frag");
 engine_=program("fullscreen.vert","engine.frag");
 plasma_=program("fullscreen.vert","sculpture.frag");
 city_=program("fullscreen.vert","city.frag");structure_=program("fullscreen.vert","structure.frag");
 cosmic_=program("fullscreen.vert","cosmic.frag");ocean_=program("fullscreen.vert","ocean.frag");
 armada_=program("fullscreen.vert","armada.frag");
 act2_=program("fullscreen.vert","act2.frag");act3_=program("fullscreen.vert","act3.frag");
 swarm_=program("swarm.vert","swarm.frag");flight_=program("flight.vert","swarm.frag");
 spectrum_=program("spectrum.vert","swarm.frag");
 particle_=program("tunnel.vert","tunnel.frag");
 glenz_=program("glenz.vert","glenz.frag");
 blurProgram_=program("fullscreen.vert","blur.frag");
 composite_=program("fullscreen.vert","composite.frag");
 text_=program("text.vert","text.frag");
 std::vector<float> planes;for(auto p:glenzPlanes())planes.insert(planes.end(),{p.x,p.y,p.z,p.w});
 gl::UseProgram(engine_);gl::Uniform4fv(gl::GetUniformLocation(engine_,"uPlanes"),24,planes.data());
 gl::GenVertexArrays(1,&vao_);gl::GenBuffers(1,&buffer_);
 gl::GenVertexArrays(1,&meshVao_);gl::GenBuffers(1,&meshBuffer_);
 const auto mesh=glenzMesh();std::vector<float> vertices;
 for(size_t i=0;i<mesh.indices.size();i+=3) {
  auto a=mesh.vertices[mesh.indices[i]],b=mesh.vertices[mesh.indices[i+1]],c=mesh.vertices[mesh.indices[i+2]];
  Vec3 u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z};
  Vec3 n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
  float len=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);n.x/=len;n.y/=len;n.z/=len;
  for(int j=0;j<3;++j) {auto p=mesh.vertices[mesh.indices[i+j]];vertices.insert(vertices.end(),{p.x,p.y,p.z,n.x,n.y,n.z,j==0?1.f:0.f,j==1?1.f:0.f,j==2?1.f:0.f});}
 }
 meshCount_=int(vertices.size()/9);
 gl::BindVertexArray(meshVao_);gl::BindBuffer(GL_ARRAY_BUFFER,meshBuffer_);
 gl::BufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(float),vertices.data(),GL_STATIC_DRAW);
 for(int i=0;i<3;++i) {gl::EnableVertexAttribArray(i);gl::VertexAttribPointer(i,3,GL_FLOAT,GL_FALSE,9*sizeof(float),reinterpret_cast<void*>(size_t(i)*3*sizeof(float)));}
 glEnable(GL_PROGRAM_POINT_SIZE);
 if(rtEnabled){rt_=std::make_unique<HardwareRT>();if(!rt_->initialize(assets_/"shaders"/"rt_scene.comp.spv",rtValidation,rtStatus_))rt_.reset();}
 else rtStatus_="disabled with --no-rt";
 if(rtRequired_&&!rt_)throw std::runtime_error("Hardware RT required: "+rtStatus_);
 std::cerr<<"Tracing renderer: "<<(rt_?rt_->diagnostics():"OPENGL FALLBACK / "+rtStatus_)<<'\n';
}
void Renderer::release(Target& t) {
 if(t.fbo)gl::DeleteFramebuffers(1,&t.fbo);if(t.texture)glDeleteTextures(1,&t.texture);t={};
}
Renderer::~Renderer() {
 if(!gl::DeleteFramebuffers) return;
 release(scene_);release(blur_[0]);release(blur_[1]);release(streak_[0]);release(streak_[1]);release(output_);
 for(GLuint p:{background_,engine_,plasma_,particle_,glenz_,blurProgram_,composite_,text_,city_,structure_,cosmic_,ocean_,armada_,swarm_,flight_,spectrum_,act2_,act3_}) if(p)gl::DeleteProgram(p);
 if(buffer_)gl::DeleteBuffers(1,&buffer_);if(meshBuffer_)gl::DeleteBuffers(1,&meshBuffer_);
 if(vao_)gl::DeleteVertexArrays(1,&vao_);if(meshVao_)gl::DeleteVertexArrays(1,&meshVao_);
}
void Renderer::resize(int w,int h) {width_=std::max(1,w);height_=std::max(1,h);}
void Renderer::target(Target& t,int w,int h,bool nearest) {
 if(t.w==w&&t.h==h) return;
 release(t);t.w=w;t.h=h;
 glGenTextures(1,&t.texture);glBindTexture(GL_TEXTURE_2D,t.texture);
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_FLOAT,nullptr);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,nearest?GL_NEAREST:GL_LINEAR);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,nearest?GL_NEAREST:GL_LINEAR);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 gl::GenFramebuffers(1,&t.fbo);gl::BindFramebuffer(GL_FRAMEBUFFER,t.fbo);
 gl::FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t.texture,0);
 if(gl::CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("OpenGL render target incomplete");
}
void Renderer::uniforms(GLuint p,double time,int scene,bool retro) {
 gl::UseProgram(p);gl::Uniform1f(gl::GetUniformLocation(p,"uTime"),float(frame_.motionSeconds));
 gl::Uniform2f(gl::GetUniformLocation(p,"uResolution"),float(uniformW_?uniformW_:width_),float(uniformH_?uniformH_:height_));
 gl::Uniform1i(gl::GetUniformLocation(p,"uScene"),scene);gl::Uniform1i(gl::GetUniformLocation(p,"uRetro"),retro);
 // Use the same exact musical clock in interactive playback and offline export.
 // Callback RMS is diagnostic only; mixing it here held loud tracks near1.
 gl::Uniform1f(gl::GetUniformLocation(p,"uEnergy"),frame_.energy);
 const float pulses[]={frame_.kick,frame_.snare,frame_.treble,frame_.pulsePhase};
 gl::Uniform4fv(gl::GetUniformLocation(p,"uPulse"),1,pulses);
 gl::Uniform1f(gl::GetUniformLocation(p,"uDrive"),frame_.drive);
 gl::Uniform1f(gl::GetUniformLocation(p,"uBass"),frame_.bass);
 gl::Uniform1f(gl::GetUniformLocation(p,"uHit"),frame_.hit);
 gl::Uniform1f(gl::GetUniformLocation(p,"uProgress"),frame_.progress);
 gl::Uniform1f(gl::GetUniformLocation(p,"uChapterProgress"),frame_.chapterProgress);
 gl::Uniform1f(gl::GetUniformLocation(p,"uChapterTime"),float(frame_.localSeconds));
 gl::Uniform1f(gl::GetUniformLocation(p,"uAccentTime"),float(frame_.accentSeconds));
 gl::Uniform1f(gl::GetUniformLocation(p,"uChapterDuration"),float(frame_.chapterDuration));
 gl::Uniform1f(gl::GetUniformLocation(p,"uEvent"),frame_.event);
 gl::Uniform1f(gl::GetUniformLocation(p,"uAction"),frame_.action);
 gl::Uniform1i(gl::GetUniformLocation(p,"uVariant"),frame_.variant);
 gl::Uniform1f(gl::GetUniformLocation(p,"uCut"),frame_.cut);
 gl::Uniform1f(gl::GetUniformLocation(p,"uTheme"),float(frame_.chapter%6));
 gl::Uniform1i(gl::GetUniformLocation(p,"uShot"),frame_.shot);
 gl::Uniform1fv(gl::GetUniformLocation(p,"uSpectrum[0]"),24,frame_.spectrum.data());
 // Tracked beat grid and beat-synced camera shots (see timeline.cpp).
 const float beat[]={frame_.beatPhase,frame_.barPhase,float(frame_.shotIndex),float(frame_.shotSeconds)};
 gl::Uniform4fv(gl::GetUniformLocation(p,"uBeat"),1,beat);
 gl::Uniform2f(gl::GetUniformLocation(p,"uBeatInfo"),frame_.beatSeconds,frame_.shotCut);
 // Transient-free camera signals (see timeline.hpp): cameras never read kicks.
 gl::Uniform2f(gl::GetUniformLocation(p,"uCamera"),frame_.cameraDrive,frame_.shotDrive);
 gl::Uniform1f(gl::GetUniformLocation(p,"uFlowTime"),float(frame_.flowSeconds));
}
void Renderer::quad(GLuint p) {gl::UseProgram(p);gl::BindVertexArray(vao_);glDrawArrays(GL_TRIANGLES,0,3);}
void Renderer::render(const TimelineFrame& input,double seconds,bool retro) {
 frame_=input;
 frame_.kick=std::clamp(input.kick*pulseGain_,0.f,1.f);
 frame_.snare=std::clamp(input.snare*pulseGain_,0.f,1.f);
 frame_.treble=std::clamp(input.treble*pulseGain_,0.f,1.f);
 frame_.bass=std::clamp(input.bass+.72f*(frame_.kick-input.kick),0.f,1.f);
 frame_.hit=std::clamp(input.hit*pulseGain_,0.f,1.f);
 const auto& frame=frame_;
 auto extent=renderExtent(width_,height_,retro,renderScale_);target(scene_,extent.width,extent.height,retro);
 target(output_,width_,height_,false);
 if(!retro) {
  // Bloom from the scene's resolution; the anamorphic streak chain is wide and short.
  const int bw=std::max(1,scene_.w/2),bh=std::max(1,scene_.h/2);
  target(blur_[0],bw,bh,false);target(blur_[1],bw,bh,false);
  target(streak_[0],std::max(1,scene_.w/4),std::max(1,scene_.h/8),false);target(streak_[1],std::max(1,scene_.w/4),std::max(1,scene_.h/8),false);
 }
 gl::BindFramebuffer(GL_FRAMEBUFFER,scene_.fbo);glViewport(0,0,scene_.w,scene_.h);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
 // Modern scenes render at the scene target's size. A 2.39:1 letterbox is
 // cropped from the 16:9 frame, so the bars are never shaded at all.
 const bool matte=letterbox_&&!retro;
 if(!retro){uniformW_=scene_.w;uniformH_=scene_.h;}
 if(matte){const int visible=std::min(scene_.h,int(std::ceil(scene_.w/2.39))+4);glEnable(GL_SCISSOR_TEST);glScissor(0,(scene_.h-visible)/2,scene_.w,visible);}
 const GLuint worlds[]={background_,plasma_,engine_,city_,structure_,cosmic_,ocean_,armada_};
 GLuint p=frame.scene>=12?act3_:(frame.scene>=8?act2_:worlds[std::clamp(frame.scene,0,7)]);
 if(frame.act==2&&rt_){
  try{const float* hdr=rt_->render(frame,scene_.w,scene_.h);glBindTexture(GL_TEXTURE_2D,scene_.texture);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,scene_.w,scene_.h,GL_RGBA,GL_FLOAT,hdr);}
  catch(const std::exception& e){if(rtRequired_)throw;rtStatus_=e.what();std::cerr<<"RT stopped; OpenGL fallback: "<<rtStatus_<<'\n';rt_.reset();uniforms(p,seconds,frame.scene,retro);quad(p);}
 }else{uniforms(p,seconds,frame.scene,retro);quad(p);}
 if(frame.scene==0&&frame.variant>=3) {
  auto dots=makeTunnel(frame.flowSeconds);uniforms(particle_,seconds,0,retro);
  gl::BindVertexArray(vao_);gl::BindBuffer(GL_ARRAY_BUFFER,buffer_);
  gl::BufferData(GL_ARRAY_BUFFER,dots.size()*sizeof(Particle),dots.data(),GL_STREAM_DRAW);
  gl::EnableVertexAttribArray(0);gl::VertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,sizeof(Particle),nullptr);
  glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);glDrawArrays(GL_POINTS,0,int(dots.size()));glDisable(GL_BLEND);
 } else if(frame.scene==2&&retro) {
  uniforms(glenz_,seconds,2,retro);gl::BindVertexArray(meshVao_);
  glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glEnable(GL_CULL_FACE);
  glCullFace(GL_FRONT);glDrawArrays(GL_TRIANGLES,0,meshCount_);glCullFace(GL_BACK);glDrawArrays(GL_TRIANGLES,0,meshCount_);
  glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
 }
 // Only the last corridor runs need this extra raster trail layer.
 // Fleet silhouettes and the city supply their own motion and depth cues.
 if(frame.scene==0&&frame.variant>=3){
 GLuint overlay=(frame.scene==1||frame.scene==2||frame.scene==5)?swarm_:flight_;
 uniforms(overlay,seconds,frame.scene,retro);gl::BindVertexArray(vao_);
 glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);
 glDrawArrays(GL_LINES,0,retro?600:1800);
 glDisable(GL_BLEND);
 }
 glDisable(GL_SCISSOR_TEST);uniformW_=uniformH_=0;
 if(!retro) {
  for(int i=0;i<6;++i) {
   Target& dst=blur_[i%2];gl::BindFramebuffer(GL_FRAMEBUFFER,dst.fbo);glViewport(0,0,dst.w,dst.h);
   gl::UseProgram(blurProgram_);gl::ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,i==0?scene_.texture:blur_[(i-1)%2].texture);
   gl::Uniform1i(gl::GetUniformLocation(blurProgram_,"uImage"),0);gl::Uniform1i(gl::GetUniformLocation(blurProgram_,"uExtract"),i==0);
   gl::Uniform2f(gl::GetUniformLocation(blurProgram_,"uDirection"),i%2==0?1.f/dst.w:0,i%2?1.f/dst.h:0);quad(blurProgram_);
  }
  // Anamorphic lens streaks: bright light smeared horizontally at growing radii.
  for(int i=0;i<4;++i) {
   Target& dst=streak_[i%2];gl::BindFramebuffer(GL_FRAMEBUFFER,dst.fbo);glViewport(0,0,dst.w,dst.h);
   gl::UseProgram(blurProgram_);gl::ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,i==0?scene_.texture:streak_[(i-1)%2].texture);
   gl::Uniform1i(gl::GetUniformLocation(blurProgram_,"uImage"),0);gl::Uniform1i(gl::GetUniformLocation(blurProgram_,"uExtract"),i==0?2:0);
   gl::Uniform2f(gl::GetUniformLocation(blurProgram_,"uDirection"),float(1<<(i*2))/dst.w,0.f);quad(blurProgram_);
  }
 }
 gl::BindFramebuffer(GL_FRAMEBUFFER,output_.fbo);glViewport(0,0,width_,height_);uniforms(composite_,seconds,frame.scene,retro);
 gl::ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,scene_.texture);
 gl::ActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,retro?scene_.texture:blur_[1].texture);
 gl::ActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,retro?scene_.texture:streak_[1].texture);gl::ActiveTexture(GL_TEXTURE0);
 gl::Uniform1i(gl::GetUniformLocation(composite_,"uImage"),0);gl::Uniform1i(gl::GetUniformLocation(composite_,"uBloom"),1);
 gl::Uniform1i(gl::GetUniformLocation(composite_,"uStreak"),2);gl::Uniform1f(gl::GetUniformLocation(composite_,"uMatte"),matte?1.f:0.f);
 gl::Uniform1f(gl::GetUniformLocation(composite_,"uFade"),frame.transition);quad(composite_);
 float scale=std::max(1.f,width_/640.f);
 double clock=frame.actSeconds,duration=actDuration(frame.act);
 float titleAlpha=float(std::clamp(std::min((9.-clock)/2.,(duration-clock)/.8),0.,1.))*frame.transition;
 if(clock>duration-6.)titleAlpha=frame.transition;
 if(titleAlpha>0){
  emblem(width_*.045f,height_*.065f,scale*21.f,titleAlpha);
  label("PULSE ASCENDANT",width_*.045f+scale*25.f,height_*.065f,scale*2.f,.82f,.94f,1.f,titleAlpha);
  const char* subtitle=frame.act==1?(clock>duration-6.?"ACT I COMPLETE / FULL THROTTLE":"ACT I / FULL THROTTLE / LUMINOUS FLOW"):(clock>duration-6.?"ACT II COMPLETE / ADVENTUROUS UPLIFT":"ACT II / ADVENTUROUS UPLIFT");
  if(frame.act==3)subtitle=clock>duration-6.?"THE ASCENT COMPLETE / RIGID ARPEGGIOS":"ACT III / REALITY ASCENDANT / RIGID ARPEGGIOS";
  label(subtitle,width_*.045f,height_*.065f+scale*19,scale*.8f,.3f,.62f,.72f,titleAlpha);
 }
 float caption=std::clamp(float(std::min(frame.localSeconds/.2,(3.2-frame.localSeconds)/.6)),0.f,1.f)*frame.transition;
 if(caption>0)label(std::to_string(frame.chapter+1)+" / "+frame.title,width_*.045f,height_*.86f,scale*1.1f,.65f,.9f,1.f,caption);
 if(retro)label("RETRO",width_*.86f,height_*.07f,scale,.7f,.9f,1.f);
 if(frame.act==2)label(rt_?"VULKAN / HARDWARE RAY TRACING":"OPENGL / RAYMARCH FALLBACK",width_*.045f,height_*.91f,scale*.65f,.45f,.7f,.85f);
 if((clock<8.&&hints_)||silent_)label(silent_?"SILENT MODE / AUDIO UNAVAILABLE":"F1 F2 F3 ACT   1-8 9 0 - = WORLD   ARROWS CHAPTER   TAB RETRO   F11 FULLSCREEN",width_*.045f,height_*.94f,scale*.65f,.45f,.6f,.7f);
 gl::BindFramebuffer(GL_READ_FRAMEBUFFER,output_.fbo);gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
 gl::BlitFramebuffer(0,0,width_,height_,0,0,width_,height_,GL_COLOR_BUFFER_BIT,GL_NEAREST);
 gl::BindFramebuffer(GL_FRAMEBUFFER,0);
 GLenum error=glGetError();if(error!=GL_NO_ERROR)throw std::runtime_error("OpenGL error "+std::to_string(error));
}
void Renderer::readPixelsRGB(std::vector<unsigned char>& pixels) {
 pixels.resize(size_t(width_)*height_*3);
 gl::BindFramebuffer(GL_READ_FRAMEBUFFER,output_.fbo);
 glPixelStorei(GL_PACK_ALIGNMENT,1);
 glReadPixels(0,0,width_,height_,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
 glPixelStorei(GL_PACK_ALIGNMENT,4);
 gl::BindFramebuffer(GL_READ_FRAMEBUFFER,0);
 if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Video frame readback failed");
}
void Renderer::capture(const std::filesystem::path& path) {
 std::filesystem::create_directories(path.parent_path());
 std::vector<unsigned char> pixels(size_t(width_)*height_*4),flipped(pixels.size());
 // Default-framebuffer pixels can be undefined for occluded/hidden windows (WSLg/Mesa).
 gl::BindFramebuffer(GL_READ_FRAMEBUFFER,output_.fbo);
 glReadPixels(0,0,width_,height_,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
 gl::BindFramebuffer(GL_READ_FRAMEBUFFER,0);
 for(int y=0;y<height_;++y)std::copy_n(pixels.data()+size_t(y)*width_*4,width_*4,flipped.data()+size_t(height_-y-1)*width_*4);
 SDL_Surface* surface=SDL_CreateSurfaceFrom(width_,height_,SDL_PIXELFORMAT_RGBA32,flipped.data(),width_*4);
 if(!surface)throw std::runtime_error(SDL_GetError());
 bool ok=SDL_SaveBMP(surface,path.string().c_str());SDL_DestroySurface(surface);
 if(!ok)throw std::runtime_error(SDL_GetError());
}
std::string Renderer::diagnostics() const {
 return std::string(reinterpret_cast<const char*>(glGetString(GL_VENDOR)))+" / "+reinterpret_cast<const char*>(glGetString(GL_RENDERER))+" / "+reinterpret_cast<const char*>(glGetString(GL_VERSION))+std::string(" | ")+(rt_?rt_->diagnostics():"OpenGL fallback");
}
void Renderer::emblem(float x,float y,float size,float opacity){
 for(int facet=0;facet<3;++facet){
  const float l=x+facet*size*.29f,top=y+size*(.42f-facet*.2f),w=size*.23f,h=size*.63f;
  const float points[]={l,top+h,l+w,top,l+w,top+h*.6f,l,top+h,l+w,top+h*.6f,l+w*.42f,top+h*1.12f};
  std::vector<float> vertices;for(int i=0;i<12;i+=2){vertices.push_back(points[i]/width_*2-1);vertices.push_back(1-points[i+1]/height_*2);}
  gl::UseProgram(text_);gl::Uniform3f(gl::GetUniformLocation(text_,"uColor"),facet==2?.95f:.15f,facet==2?.68f:.8f,facet==2?.3f:1.f);
  gl::Uniform1f(gl::GetUniformLocation(text_,"uOpacity"),opacity);
  gl::BindVertexArray(vao_);gl::BindBuffer(GL_ARRAY_BUFFER,buffer_);gl::BufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(float),vertices.data(),GL_STREAM_DRAW);
  gl::EnableVertexAttribArray(0);gl::VertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),nullptr);
  glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDrawArrays(GL_TRIANGLES,0,6);glDisable(GL_BLEND);
 }
}
void Renderer::label(const std::string& text,float x,float y,float s,float r,float g,float b,float opacity) {
 static const std::map<char,std::array<unsigned char,7>> font={
 {'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},{'C',{14,17,16,16,16,17,14}},
 {'D',{30,17,17,17,17,17,30}},{'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},
 {'G',{14,17,16,23,17,17,15}},{'H',{17,17,17,31,17,17,17}},{'I',{14,4,4,4,4,4,14}},
 {'J',{7,2,2,2,18,18,12}},{'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},
 {'M',{17,27,21,21,17,17,17}},{'N',{17,25,21,19,17,17,17}},{'O',{14,17,17,17,17,17,14}},
 {'P',{30,17,17,30,16,16,16}},{'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},
 {'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},{'U',{17,17,17,17,17,17,14}},
 {'V',{17,17,17,17,17,10,4}},{'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},
 {'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}},
 {'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},{'2',{14,17,1,2,4,8,31}},
 {'3',{30,1,1,14,1,1,30}},{'4',{2,6,10,18,31,2,2}},{'5',{31,16,16,30,1,1,30}},
 {'6',{14,16,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},{'8',{14,17,17,14,17,17,14}},
 {'9',{14,17,17,15,1,1,14}},{'/',{1,1,2,4,8,16,16}},{'-',{0,0,0,31,0,0,0}},{'=',{0,0,31,0,31,0,0}}};
 std::vector<float> vertices;
 for(char ch:text) {
  auto it=font.find(ch);
  if(it!=font.end())for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(it->second[row]&(16>>col)) {
   float l=(x+col*s)/width_*2-1,rr=(x+(col+1)*s)/width_*2-1;
   float top=1-(y+row*s)/height_*2,bottom=1-(y+(row+1)*s)/height_*2;
   vertices.insert(vertices.end(),{l,top,l,bottom,rr,bottom,l,top,rr,bottom,rr,top});
  }
  x+=s*6;
 }
 gl::UseProgram(text_);gl::Uniform3f(gl::GetUniformLocation(text_,"uColor"),r,g,b);
 gl::Uniform1f(gl::GetUniformLocation(text_,"uOpacity"),opacity);
 gl::BindVertexArray(vao_);gl::BindBuffer(GL_ARRAY_BUFFER,buffer_);gl::BufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(float),vertices.data(),GL_STREAM_DRAW);
 gl::EnableVertexAttribArray(0);gl::VertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),nullptr);
 glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
 glDrawArrays(GL_TRIANGLES,0,int(vertices.size()/2));glDisable(GL_BLEND);
}
}
