#version 330 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec3 aBary;
uniform float uTime;uniform vec2 uResolution;uniform int uMirror;
out vec3 vNormal;out vec3 vPosition;out vec3 vBary;
uniform int uRetro;
#include "common.glsl"
mat3 rotateX(float a) {float c=cos(a),s=sin(a);return mat3(1,0,0,0,c,s,0,-s,c);}
mat3 rotateY(float a) {float c=cos(a),s=sin(a);return mat3(c,0,-s,0,1,0,s,0,c);}
void main() {
 mat3 m=rotateY(uTime*.27)*rotateX(uTime*.19+.3);
 vec3 p=m*aPosition*.55;
 p.y+=sin(uTime*.55)*.1;
 if(uMirror==1)p.y=-p.y-3.3;
 vec3 eye=orbitEye(uTime,4.9),view=transpose(cameraBasis(eye,vec3(0)))*(p-eye);
 view.xy=rot(-sin(uTime*.19)*.12*uCamera.x)*view.xy;
 gl_Position=vec4(view.x*1.8*uResolution.y/uResolution.x,view.y*1.8,0.,view.z);
 vNormal=m*aNormal;vPosition=p;vBary=aBary;
}
