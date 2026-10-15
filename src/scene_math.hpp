#pragma once
#include <vector>
namespace remix {
struct Vec3 { float x,y,z; };
struct Plane {float x,y,z,w;};
struct Particle { float x,y,z,brightness; };
struct Mesh { std::vector<Vec3> vertices; std::vector<unsigned> indices; };
struct Extent { int width,height; };
std::vector<Particle> makeTunnel(double seconds);
Mesh glenzMesh();
std::vector<Plane> glenzPlanes();
Extent renderExtent(int width,int height,bool retro,float scale=1);
}
