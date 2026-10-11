#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;
#include "common.glsl"
void main(){
 vec2 p=(vUV*2.-1.)*vec2(uResolution.x/uResolution.y,1.);
 vec3 ro=orbitEye(uTime,7.2),rd=orbitRay(p,ro,uTime);
 vec3 c=vec3(.002,.004,.015);
 float density=0.,transmission=1.;
 for(int i=0;i<72;i++){
  float t=float(i)*.16;vec3 at=ro+rd*t;
  at.xy=rot(uTime*.12)*at.xy;at.yz=rot(.35+sin(uTime*.08)*.3)*at.yz;
  float band=frequencyBand((atan(at.z,at.x)/PI+1.)*11.99);
  float radius=length(at),ring=length(vec2(length(at.xz)-2.15-band*.25,at.y*2.2));
  float disk=exp(-ring*8.)*(.55+.45*sin(atan(at.z,at.x)*12.+uTime*4.+radius*6.));
  float corona=exp(-abs(radius-1.15-uBass*.18)*18.);
  vec3 fire=mix(vec3(2.4,.22,.015),vec3(.05,.65,2.5),.5+.5*sin(at.x*2.+uTheme));
  c+=fire*(disk*.18+corona*.11)*transmission*(.45+band*1.25+uBass*.4+uHit*.3);
  transmission*=1.-clamp(exp(-max(radius-1.,0.)*25.)*.45,0.,.45);
  density+=disk*.16;
 }
 vec2 q=rd.xy/max(.15,abs(rd.z));
 float clouds=fbm(q*2.+uTime*.015)*fbm(q*5.-uTime*.02);
 c+=accent(q.y+uTime*.03)*clouds*.15*transmission;
 vec2 cell=floor(q*300.);float star=step(.997,hash(cell));
 c+=vec3(.35,.6,1.)*star*transmission;
 color=vec4(c,1);
}
