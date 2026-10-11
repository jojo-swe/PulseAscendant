#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;
#include "common.glsl"
float box(vec3 p,vec3 b){vec3 q=abs(p)-b;return length(max(q,0.))+min(max(q.x,max(q.y,q.z)),0.);}
vec2 city(vec3 p) {
 vec2 cell=floor((p.xz+4.)/8.);vec2 q=mod(p.xz+4.,8.)-4.;
 float h=3.+hash(cell)*18.+frequencyBand(mod(abs(cell.y)+abs(cell.x)*5.,24.))*2.5;
 float b=box(vec3(q.x,p.y-h*.5,q.y),vec3(2.5,h*.5,2.5));
 if(abs(cell.x)<1.)b=100.;
 float track=abs(p.x)-3.2;
 float rail=length(vec2(track,p.y-.14))-.065;
 float ground=p.y+.15;
 if(rail<ground&&rail<b)return vec2(rail,3.);
 return ground<b?vec2(ground,1.):vec2(b,2.);
}
vec3 normalAt(vec3 p){vec2 e=vec2(.007,0);return normalize(vec3(city(p+e.xyy).x-city(p-e.xyy).x,city(p+e.yxy).x-city(p-e.yxy).x,city(p+e.yyx).x-city(p-e.yyx).x));}
void main() {
 vec2 p=(vUV*2.-1.)*vec2(uResolution.x/uResolution.y,1.);
 float travel=uTime*24.;
 float height=uShot==2?8.+sin(uProgress*PI)*9.:1.15+float(uShot==1)*1.7;
 vec3 ro=vec3(sin(uTime*.16)*1.4,height,travel);
 p=rot(flightBank(uTime)*.7)*p;
 vec3 rd=cameraBasis(ro,ro+vec3(sin(uTime*.11)*2.,uShot==2?-3.:-.04,20.))*normalize(vec3(p,1.25));
 vec3 sky=mix(vec3(.006,.008,.04),vec3(.09,.015,.13),max(0.,1.-abs(rd.y)*3.));
 sky+=accent(.6)*pow(max(dot(rd,normalize(vec3(.2,.08,1.))),0.),100.)*.25;
 float t=.02;vec2 h;bool hit=false;
 for(int i=0;i<128;i++){h=city(ro+rd*t);if(h.x<.007){hit=true;break;}t+=max(.008,h.x*.8);if(t>160.)break;}
 vec3 c=sky;
 if(hit){
  vec3 at=ro+rd*t,n=normalAt(at);float fres=pow(1.-max(dot(n,-rd),0.),3.);
  vec3 neon=accent(at.z*.012);
  c=vec3(.018,.026,.055)*(.5+max(dot(n,normalize(vec3(-1,2,-1))),0.));
  if(h.y>1.5&&h.y<2.5){
   vec2 grid=abs(fract(vec2((abs(n.x)>.5?at.z:at.x)*1.5,at.y*.8))-.5);
   float window=step(.12,grid.x)*step(.12,grid.y);
   float lit=step(.25,hash(floor(at.xz*.8)+floor(at.y*.8)));
   float band=frequencyBand(mod(floor(at.y*.8)+floor(at.z/8.),24.));
   c+=neon*(window*lit*(.06+band*.32)+fres*.3);
   float edge=1.-smoothstep(.015,.06,min(abs(mod(at.x+4.,8.)-1.5),abs(mod(at.z+4.,8.)-1.5)));
   c+=neon*edge*(.5+uBass);
  }else if(h.y>2.5)c=neon*(2.+uHit*2.);
  else {
   float lane=line(abs(at.x)-.1,.035)*step(.6,fract(at.z*.08));
   float grid=line(min(abs(mod(at.x+.5,1.)-.5),abs(mod(at.z+2.,4.)-2.)),.018);
   c+=neon*(lane*1.8+grid*.08+exp(-abs(abs(at.x)-3.2)*2.)*.35);
   c+=neon*.13*pow(max(0.,1.-abs(rd.x)),15.);
  }
  c=mix(c,sky,1.-exp(-t*.012));
 }
 color=vec4(c,1);
}
