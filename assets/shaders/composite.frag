#version 330 core
in vec2 vUV;out vec4 color;
uniform sampler2D uImage,uBloom;uniform int uRetro;uniform float uFade,uTime;
#include "common.glsl"
void main() {
 vec2 centre=vUV-.5;
 float speed=(uDrive*.0035+uCut*.018+uHit*.001)*float(uRetro==0);
 vec3 c=vec3(0);
 for(int i=0;i<6;i++)c+=texture(uImage,vUV-centre*speed*float(i)).rgb/6.;
 if(uRetro==0) {
  float ca=.0012*(uDrive+uHit)*length(centre);
  c.r=mix(c.r,texture(uImage,vUV+centre*ca).r,.35);
  c.b=mix(c.b,texture(uImage,vUV-centre*ca).b,.35);
  c+=texture(uBloom,vUV).rgb*(.55+uBass*.2);
  c+=vec3(.02,.035,.05)*uCut;
 }
 float vig=pow(16.*vUV.x*vUV.y*(1.-vUV.x)*(1.-vUV.y),.17);
 c*=mix(.48,1.,vig);
 // ACES-inspired bounded filmic curve, followed by gamma encoding.
 c=clamp((c*(2.51*c+.03))/(c*(2.43*c+.59)+.14),0.,1.);
 c=pow(c,vec3(1./2.2))*uFade;
 if(uRetro==1) c*=.92+.08*sin(vUV.y*200.*3.14159265);
 color=vec4(c,1.);
}
