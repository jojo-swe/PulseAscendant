#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;
#include "common.glsl"
float waves(vec2 p){return sin(p.x*.45+uTime)*(.15+uSpectrum[2]*.3)+sin(p.y*.32-uTime*1.2)*(.2+uSpectrum[5]*.3)+sin(dot(p,vec2(.8,.6))+uTime*1.6)*(.06+midrange()*.12);}
vec3 sky(vec3 rd){
 float sun=pow(max(dot(rd,normalize(vec3(.2,.18,1))),0.),160.);
 vec3 c=mix(vec3(.01,.02,.07),vec3(.16,.025,.17),exp(-abs(rd.y)*8.));
 c+=vec3(1.5,.32,.04)*sun;
 float aurora=pow(.5+.5*sin(rd.x*15.+rd.y*8.+sin(rd.x*12.+uTime*.1)*2.),15.);
 c+=accent(rd.x*2.)*aurora*(.05+frequencyBand((rd.x+1.)*12.)*.3)*smoothstep(0.,.4,rd.y);
 return c;
}
vec2 scene(vec3 p){
 float water=(p.y-waves(p.xz))*.55;
 vec2 cell=floor((p.xz+6.)/12.),q=mod(p.xz+6.,12.)-6.;
 float band=frequencyBand(mod(abs(cell.x)+abs(cell.y)*3.,24.));
 float sphere=length(vec3(q.x,p.y-3.2-sin(uTime*.3+hash(cell)*6.)*.6,q.y))-(.45+band*.25);
 if(abs(cell.x)<1.)sphere=100.;
 return water<sphere?vec2(water,1):vec2(sphere,2);
}
void main(){
 vec2 p=(vUV*2.-1.)*vec2(uResolution.x/uResolution.y,1.);p=rot(flightBank(uTime)*.4)*p;
 vec3 ro=vec3(sin(uTime*.14)*2.,3.3+sin(uProgress*PI),uTime*9.);
 vec3 rd=cameraBasis(ro,ro+vec3(sin(uTime*.2),-.9,15))*normalize(vec3(p,1.4));
 float t=.02;vec2 h;bool found=false;
 for(int i=0;i<112;i++){h=scene(ro+rd*t);if(h.x<.006){found=true;break;}t+=clamp(h.x,.008,2.);if(t>130.)break;}
 vec3 c=sky(rd);
 if(found){
  vec3 at=ro+rd*t,n;
  if(h.y>1.5){vec2 q=mod(at.xz+6.,12.)-6.;n=normalize(vec3(q.x,at.y-3.2,q.y));c=accent(at.z*.03)*(1.2+uBass)+sky(reflect(rd,n));}
  else {
   vec2 e=vec2(.04,0);n=normalize(vec3(waves(at.xz-e.xy)-waves(at.xz+e.xy),.08,waves(at.xz-e.yx)-waves(at.xz+e.yx)));
   float fres=.08+.92*pow(1.-max(dot(n,-rd),0.),5.);
   c=mix(vec3(.005,.045,.065),sky(reflect(rd,n)),fres);
   float foam=pow(.5+.5*sin(at.x*.9+at.z*.7+waves(at.xz)*6.-uTime*2.),20.);
   float rings=pow(.5+.5*sin(length(at.xz-vec2(0,uTime*9.))*1.8-uTime*4.),30.);
   c+=accent(at.z*.015)*(foam*.3+rings*.15)*(1.+uBass+uHit*.5);
  }
  c=mix(c,sky(rd),1.-exp(-t*.012));
 }
 color=vec4(c,1);
}
