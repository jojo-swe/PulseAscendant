#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;
#include "common.glsl"
float box(vec3 p,vec3 b){vec3 q=abs(p)-b;return length(max(q,0.))+min(max(q.x,max(q.y,q.z)),0.);}
vec3 fleetCell(vec3 p){p.z=mod(p.z+7.,14.)-7.;p.x=abs(mod(p.x+6.,12.)-6.)-3.3;p.y=mod(p.y+4.5,9.)-4.5;return p;}
vec2 fleet(vec3 p){
 vec3 q=fleetCell(p);
 float body=box(q,vec3(.3,.18,1.5));
 float wings=box(q+vec3(0,.07,.2),vec3(1.1,.055,.5));
 float cockpit=length((q-vec3(0,.22,.6))*vec3(1,1, .65))-.25;
 float engine=length(q.xy)-.17;engine=max(engine,abs(q.z+2.8)-1.3);
 float ship=min(body,wings);
 if(cockpit<ship){ship=cockpit;}
 return engine<ship?vec2(engine,2):vec2(ship,1);
}
vec3 normalAt(vec3 p){vec2 e=vec2(.004,0);return normalize(vec3(fleet(p+e.xyy).x-fleet(p-e.xyy).x,fleet(p+e.yxy).x-fleet(p-e.yxy).x,fleet(p+e.yyx).x-fleet(p-e.yyx).x));}
void main(){
 vec2 p=(vUV*2.-1.)*vec2(uResolution.x/uResolution.y,1.);p=rot(flightBank(uTime))*p;
 vec3 ro=vec3(sin(uTime*.21)*.6,sin(uTime*.3)*.45,uTime*10.);
 vec3 rd=normalize(vec3(p,1.25));
 float t=.03,glow=0.;vec2 h;bool found=false;
 for(int i=0;i<128;i++){h=fleet(ro+rd*t);float dt=clamp(h.x*.8,.008,1.3);if(h.y>1.5)glow+=exp(-abs(h.x)*12.)*dt;if(h.x<.004){found=true;break;}t+=dt;if(t>115.)break;}
 vec3 neon=accent(uTime*.02);
 vec3 c=vec3(.005,.008,.025)+neon*fbm(rd.xy*3.+uTime*.01)*.04;
 if(found){
  vec3 at=ro+rd*t,n=normalAt(at),q=fleetCell(at),l=normalize(vec3(-2,3,-1));
  float spec=pow(max(dot(reflect(-l,n),-rd),0.),50.);
  c=vec3(.13,.2,.3)*(.3+max(dot(n,l),0.))+vec3(.4,.7,1.)*spec;
  float edge=pow(1.-max(dot(n,-rd),0.),3.);
  c+=neon*(edge*.6+line(abs(q.x)-.3,.015)*.4+line(q.z-.8,.02)*.35);
  if(h.y>1.5)c=neon*(1.+frequencyBand(mod(abs(floor(at.z/14.))+abs(floor(at.x/12.))*5.,24.))*3.);
  c=mix(c,vec3(.008,.012,.035),1.-exp(-t*.018));
 }
 c+=neon*glow*.12;
 color=vec4(c,1);
}
