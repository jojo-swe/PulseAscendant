#include "scene_math.hpp"
#include <algorithm>
#include <cmath>
namespace remix {
std::vector<Particle> makeTunnel(double seconds) {
 float t=std::isfinite(seconds)?float(std::fmod(seconds,3600.)):0;
 std::vector<Particle> out; out.reserve(160*96);
 // TUN10.PAS uses depth-scaled rings with 64 points and a sinusoidal moving center.
 // The port increases density and evaluates the center continuously instead of shifting a DOS table.
 for(int ring=0;ring<160;++ring) {
  float z=std::fmod(ring*0.36f-t*4.5f+60000.f,57.6f)+0.5f;
  float cx=std::sin(z*.13f+t*.36f)*1.7f+std::cos(z*.07f-t*.2f);
  float cy=std::cos(z*.16f+t*.27f)*1.4f;
  for(int j=0;j<96;++j) {
   float a=j*6.283185307f/96+t*.1f+z*.035f;
   float r=3.6f+.15f*std::sin(z*.5f+t);
   out.push_back({cx+r*std::cos(a),cy+r*std::sin(a),z,ring%8==0?1.8f:.65f});
  }
 }
 return out;
}
Mesh glenzMesh() {
 // GLENZ/MAIN.C points[] divided by 100*ZZZ; epolys[] triangle indices unchanged.
 return {{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1},
 {0,0,-1.7f},{0,0,1.7f},{1.7f,0,0},{-1.7f,0,0},{0,1.7f,0},{0,-1.7f,0}},
 {0,1,8,1,2,8,2,3,8,3,0,8,2,1,10,1,5,10,5,6,10,6,2,10,2,6,12,6,7,12,7,3,12,3,2,12,
 0,3,11,3,7,11,7,4,11,4,0,11,5,1,13,1,0,13,0,4,13,4,5,13,5,4,9,4,7,9,7,6,9,6,5,9}};
}
std::vector<Plane> glenzPlanes() {
 auto mesh=glenzMesh();std::vector<Plane> out;
 for(size_t i=0;i<mesh.indices.size();i+=3) {
  auto a=mesh.vertices[mesh.indices[i]],b=mesh.vertices[mesh.indices[i+1]],c=mesh.vertices[mesh.indices[i+2]];
  Vec3 u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z};
  Vec3 n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
  float length=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);n.x/=length;n.y/=length;n.z/=length;
  float d=n.x*a.x+n.y*a.y+n.z*a.z;
  if(d<0){n.x=-n.x;n.y=-n.y;n.z=-n.z;d=-d;}
  out.push_back({n.x,n.y,n.z,d});
 }
 return out;
}
Extent renderExtent(int width,int height,bool retro) {
 return retro?Extent{320,200}:Extent{std::clamp(width,1,8192),std::clamp(height,1,8192)};
}
}
