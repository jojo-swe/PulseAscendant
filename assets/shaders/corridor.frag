#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;uniform float uEnergy;
#include "common.glsl"
// Sphere tracing an enclosing curved tube. Source ring particles use this same centre line.
vec2 centre(float z) {return vec2(sin(z*.13+uTime*.36)*1.7+cos(z*.07-uTime*.2),cos(z*.16+uTime*.27)*1.4);}
float distanceToWall(vec3 p) {
 vec2 q=p.xy-centre(p.z);float radius=4.05+.15*sin(p.z*.5+uTime);
 // Conservative factor for the slope of the curved centre line and longitudinal ribs.
 return (radius-length(q))*.65;
}
vec3 wallNormal(vec3 p) {
 vec2 e=vec2(.003,0.);return normalize(vec3(distanceToWall(p+e.xyy)-distanceToWall(p-e.xyy),distanceToWall(p+e.yxy)-distanceToWall(p-e.yxy),distanceToWall(p+e.yyx)-distanceToWall(p-e.yyx)));
}
void main() {
 vec2 p=(vUV*2.-1.)*vec2(uResolution.x/uResolution.y,1.);
 p=rot(flightBank(uTime))*p;
 vec3 ro=vec3(flightOffset(uTime),0),rd=normalize(vec3(p,1.65-uDrive*.5-uHit*.04));
 float t=.05,volume=0.;bool found=false;
 for(int i=0;i<96;i++) {
  vec3 pos=ro+rd*t;float d=distanceToWall(pos);
  float beam=pow(.5+.5*cos(atan(pos.y-centre(pos.z).y,pos.x-centre(pos.z).x)*8.+pos.z*.08),32.);
  float stepD=clamp(d,.015,1.2);
  volume+=beam*.005*stepD*exp(-t*.035);
  if(d<.004){found=true;break;}t+=stepD;if(t>65.)break;
 }
 vec3 c=vec3(.002,.008,.018);
 if(found) {
  vec3 pos=ro+rd*t,n=wallNormal(pos),v=-rd;
  vec2 q=pos.xy-centre(pos.z);
  float angle=atan(q.y,q.x),travel=pos.z+uTime*4.5;
  float ring=abs(mod(travel+1.8,3.6)-1.8);
  float seam=abs(mod(angle*12./PI+.5,1.)-.5);
  float groove=1.-smoothstep(.012,.045,seam);
  float rib=1.-smoothstep(.025,.07,ring);
  vec3 cold=accent(0),warm=accent(PI);
  vec3 emission=mix(cold,warm,.5+.5*sin(angle+travel*.09));
  vec3 base=mix(vec3(.015,.024,.038),vec3(.09,.12,.15),fbm(vec2(angle*15.,travel*4.)));
  vec3 l=normalize(vec3(centre(pos.z-1.),pos.z-3.)-pos);
  float diffuse=max(dot(n,l),0.),spec=pow(max(dot(reflect(-l,n),v),0.),64.);
  c=base*(.2+diffuse*1.5)+vec3(.2,.5,.7)*spec;
  float band=frequencyBand((angle/PI+1.)*11.99);
  c+=emission*(rib*.85+groove*.06)*(.3+band*1.5+uHit*.4);
  c=mix(c,vec3(.015,.06,.1),1.-exp(-t*.035));
 }
 c+=vec3(.04,.25,.42)*volume;
 c+=vec3(.015,.12,.2)*pow(max(dot(rd,normalize(vec3(centre(55.),55.))),0.),110.);
 color=vec4(c,1.);
}
