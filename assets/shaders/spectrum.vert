#version 330 core
uniform vec2 uResolution;uniform float uTime;uniform int uScene;
out vec3 vColor;
uniform int uRetro;
#include "common.glsl"
// Four radial crowns of 24 wireframe columns. Each column has its own FFT band.
const ivec2 edges[12]=ivec2[12](ivec2(0,1),ivec2(1,3),ivec2(3,2),ivec2(2,0),
 ivec2(4,5),ivec2(5,7),ivec2(7,6),ivec2(6,4),ivec2(0,4),ivec2(1,5),ivec2(2,6),ivec2(3,7));
void main(){
 int column=gl_VertexID/24,band=column%24,crown=column/24;
 int corner=edges[(gl_VertexID%24)/2][gl_VertexID%2];
 float level=uSpectrum[band],angle=float(band)*2.*PI/24.+uTime*.06+float(crown)*.09;
 float radius=2.6+float(crown)*.42;
 vec3 point=vec3((float(corner&1)-.5)*.12,float((corner>>1)&1)*(.12+level*1.8), (float((corner>>2)&1)-.5)*.12);
 point.xz=rot(-angle)*point.xz+vec2(cos(angle),sin(angle))*radius;
 point.y+=(crown%2==0?-1.:1.)*(.65+float(crown)*.25);
 if(crown%2==1)point.y-=float((corner>>1)&1)*(.24+level*3.6);
 vec3 eye=orbitEye(uTime,uScene==5?7.2:4.9),view=transpose(cameraBasis(eye,vec3(0)))*(point-eye);
 view.xy=rot(-sin(uTime*.19)*.12*uCamera.x)*view.xy;
 vec2 p=view.xy/max(.1,view.z)*1.8;p.x*=uResolution.y/uResolution.x;
 gl_Position=view.z>.15?vec4(p,0,1):vec4(3,3,3,1);
 vColor=accent(float(band)*.25)*(.035+level*.2)*(float((corner>>1)&1)*.65+.35);
}
