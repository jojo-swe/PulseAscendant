#version 330 core
layout(location=0) in vec4 aParticle;
uniform vec2 uResolution; uniform float uTime; uniform int uRetro;
out vec3 vColor; out float vAlpha;
#include "common.glsl"
void main() {
 float z=aParticle.z;
 vec2 p=rot(-flightBank(uTime))*(aParticle.xy-flightOffset(uTime))/z*(1.65-uDrive*.5-uHit*.04);
 p.x*=uResolution.y/uResolution.x;
 gl_Position=vec4(p,0.,1.);
 gl_PointSize=clamp(uResolution.y*.028/z*(uRetro==1?.7:1.4),1.,32.);
 float phase=.5+.5*sin(z*.09-uTime*.3);
 float band=frequencyBand(mod(float(gl_VertexID),24.));
 vColor=mix(vec3(.03,.9,1.6),vec3(1.5,.04,.35),phase)*aParticle.w*(.3+band*1.3);
 vAlpha=smoothstep(.5,2.,z)*(1.-smoothstep(30.,58.,z));
}
