#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;uniform float uEnergy;
#include "common.glsl"
// A 3D gyroid shell coloured by the nested sine fields of the original plasma.
float shell(vec3 p) {
 p.xy=rot(uTime*.14)*p.xy;p.yz=rot(uTime*.11)*p.yz;
 vec3 q=p*(3.2+uBass*.6);
 float g=dot(sin(q+uTime*.18),cos(q.zxy-uTime*.13));
 float gyroid=(abs(g)-(.23+midrange()*.18))/6.5;
 return max(gyroid,length(p)-1.6);
}
vec2 mapS(vec3 p) {float d=shell(p);return d<p.y+1.8?vec2(d,2):vec2(p.y+1.8,1);}
vec3 norm(vec3 p) {vec2 e=vec2(.002,0);return normalize(vec3(mapS(p+e.xyy).x-mapS(p-e.xyy).x,mapS(p+e.yxy).x-mapS(p-e.yxy).x,mapS(p+e.yyx).x-mapS(p-e.yyx).x));}
float shadeShadow(vec3 p,vec3 l) {
 float s=1.,t=.02;for(int i=0;i<20;i++){float d=mapS(p+l*t).x;s=min(s,10.*d/t);t+=clamp(d,.02,.25);if(d<.001||t>4.)break;}return clamp(s,.1,1.);
}
float occlusion(vec3 p,vec3 n) {float f=0.;for(int i=1;i<=4;i++){float h=float(i)*.06;f+=max(0.,h-mapS(p+n*h).x)/float(i);}return clamp(1.-f*2.,.2,1.);}
float plasma(vec3 p) {
 return sin(p.x*3.4+sin(p.y*2.1+uTime*.32)*2.4+uTime*.24)+sin(p.y*4.+sin(p.z*2.7-uTime*.23)*2.1-uTime*.36);
}
vec3 env(vec3 rd) {
 vec3 c=vec3(.006,.012,.026)+vec3(.012,.025,.04)*max(rd.y,0.);
 c+=vec3(.1,.65,.85)*pow(max(dot(rd,normalize(vec3(-2,3,4))),0.),35.);
 c+=vec3(1.,.24,.03)*pow(max(dot(rd,normalize(vec3(3,2,2))),0.),40.);return c;
}
vec3 surface(vec3 p,vec3 n,vec3 v,float id,bool reflected) {
 float f=plasma(p);
 vec3 base=mix(vec3(.008,.3,.37),vec3(.42,.055,.012),smoothstep(-.6,1.,f));
 if(id<1.5)base=vec3(.015,.022,.03);
 vec3 l=normalize(vec3(-3,4,4)-p),h=normalize(l+v);
 float shadow=reflected?1.:shadeShadow(p+n*.015,l);
 float diffuse=max(dot(n,l),0.);
 float spec=pow(max(dot(n,h),0.),72.);
 float fresnel=pow(1.-max(dot(n,v),0.),3.);
 vec3 c=base*(.15+diffuse*2.2*shadow)*occlusion(p,n);
 c+=vec3(.6,.9,1.)*spec*2.5*shadow+env(reflect(-v,n))*(.6+fresnel);
 if(id>1.5) {
  float vein=pow(.5+.5*sin(f*7.+p.z*2.),18.);
  float band=frequencyBand((atan(p.z,p.x)/PI+1.)*11.99);
  c+=mix(vec3(.015,1.,1.8),vec3(2.,.28,.025),smoothstep(-.2,1.,f))*vein*(.2+band*2.4);
  c+=vec3(.03,.28,.36)*fresnel;
 }
 return c;
}
vec3 reflection(vec3 ro,vec3 rd) {
 float t=.03;for(int i=0;i<64;i++){vec3 p=ro+rd*t;vec2 d=mapS(p);if(d.x<.002)return surface(p,norm(p),-rd,d.y,true);t+=max(.005,d.x*.7);if(t>12.)break;}return env(rd);
}
void main() {
 vec2 p=(vUV*2.-1.)*vec2(uResolution.x/uResolution.y,1.);
 vec3 ro=orbitEye(uTime,4.9),rd=orbitRay(p,ro,uTime);
 float t=0,glow=0;bool found=false;vec2 hit;
 for(int i=0;i<128;i++) {
  vec3 pos=ro+rd*t;hit=mapS(pos);
  float stepD=clamp(hit.x*.7,.003,.5);
  if(hit.y>1.5)glow+=exp(-abs(hit.x)*20.)*stepD;
  if(hit.x<.0018){found=true;break;}t+=stepD;if(t>16.)break;
 }
 vec3 c=env(rd)*.4;
 if(found) {
  vec3 pos=ro+rd*t,n=norm(pos);c=surface(pos,n,-rd,hit.y,false);
  if(hit.y<1.5&&uRetro==0)c=mix(c,reflection(pos+n*.02,reflect(rd,n)),.45);
  c=mix(c,env(rd)*.1,1.-exp(-t*(hit.y<1.5?.22:.025)));
 }
 c+=vec3(.01,.24,.35)*glow;
 // Distant star particles in the black chamber.
 vec2 cell=floor(vUV*uResolution/3.);float h=hash(cell);
 if(!found)c+=step(.999,h)*vec3(.1,.2,.3)*(.5+.5*sin(h*100.+uTime));
 color=vec4(c,1.);
}
