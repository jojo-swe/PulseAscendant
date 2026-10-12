#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;
#include "common.glsl"
// Single-use astrophysical events: ignition, collision, blast, filaments, warp.
vec2 field(vec3 p){
 float k=clamp(uChapterProgress,0.,1.),d=0.,hot=0.;
 if(uVariant==0){
  float r=length(p),a=atan(p.y,p.x);
  float core=exp(-abs(r-(.42+smoothstep(.02,.45,k)*.45))*18.);
  float ring=exp(-length(vec2(length(p.xy)-(1.4+k*2.7),p.z*2.))*10.);
  float spokes=pow(.5+.5*cos(a*6.),45.)*exp(-abs(p.z)*7.)*exp(-abs(r-(1.+k*2.))*2.);
  d=core+ring*.65+spokes*(1.-smoothstep(.35,.8,k))*.7;
  hot=core+.3*ring;
 }else if(uVariant==1){
  float separation=2.5*(1.-clamp(k*.15+uAction*.85,0.,1.))+.13;
  vec3 q=p-vec3(separation,.32,0.),q2=p+vec3(separation,.32,0.);
  float suns=exp(-abs(length(q)-.8)*13.)+exp(-abs(length(q2)-.8)*13.);
  float bridge=exp(-length(vec2(p.y,p.z))*7.)*exp(-abs(p.x)*.5)*smoothstep(.12,.55,k);
  float nova=exp(-abs(length(p)-(.7+max(k-.25,0.)*8.*uAction))*13.)*uAction;
  d=suns*.8+bridge*.6+nova;hot=bridge+nova;
 }else if(uVariant==2){
  // A hollow, expanding shock front, then matter streams away through its centre.
  float r=length(p),shell=exp(-abs(r-(.8+k*2.8+uAction*1.6))*15.);
  float bands=.2+.8*pow(.5+.5*sin(atan(p.z,p.x)*11.+p.y*4.),4.);
  float jets=exp(-length(p.xz)*9.)*exp(-abs(p.y)*.4)*(.4+k);
  d=shell*bands+jets;hot=shell*.75;
 }else if(uVariant==3){
  vec3 q=p;q.xz=rot(k*.5)*q.xz;
  float r=length(q.xz),a=atan(q.z,q.x);
  float spiral=abs(sin(a*2.-r*2.6+k*5.));
  float arm=exp(-spiral*18.)*exp(-abs(q.y-sin(r*1.4+k*4.)*.23)*10.)*exp(-abs(r-2.1)*.7);
  float stream=exp(-length(vec2(q.x*.8,q.y-1.4*sin(q.z*.8+k*3.)))*7.);
  d=arm*.7+stream*.6;hot=stream;
 }else{
  // Longitudinal coils read as an actual departure rather than another orbit.
  float radius=length(p.xy),a=atan(p.y,p.x);
  float z=p.z+uChapterTime*(3.+k*8.);
  float coil=length(vec2(radius-(1.7+.2*sin(z*.7)),sin(z*2.-a)*.65));
  d=exp(-coil*13.)*.8;
  d+=pow(.5+.5*cos(a*9.+z*.3),32.)*exp(-abs(radius-2.4)*12.)*.5;
  hot=exp(-coil*22.);
 }
 return vec2(d,hot);
}
void main(){
 vec2 p=(vUV*2.-1.)*vec2(uResolution.x/uResolution.y,1.);float k=clamp(uChapterProgress,0.,1.);
 vec3 ro,target=vec3(0);
 if(uVariant==0)ro=vec3(.3-k*.6,.2,6.6-k*1.8);
 else if(uVariant==1)ro=vec3(-1.+k*2.,1.6,7.7);
 else if(uVariant==2)ro=vec3(5.8-k*1.5,2.6,5.2);
 else if(uVariant==3)ro=vec3(.8,5.8-k*1.3,5.7);
 else{ro=vec3(sin(k*PI)*.23,cos(k*PI)*.23,5.);target=ro+vec3(0,0,-12.);p=rot(k*.35)*p;}
 vec3 rd=cameraBasis(ro,target)*normalize(vec3(p,uVariant==4?1.15:1.65));
 vec3 c=vec3(.001,.003,.011);float transmission=1.;
 int count=uRetro==1?48:80;
 for(int i=0;i<80;i++){
  if(i>=count)break;float ds=14.4/float(count),t=(float(i)+.5)*ds;
  vec3 at=ro+rd*t;vec2 d=field(at);
  float band=frequencyBand((atan(at.z,at.x)/PI+1.)*11.99);
  vec3 cool=vec3(.02,.35,1.5),warm=vec3(2.5,.35,.035);
  // The opening uses white-blue energy; collision and blast reveal hot stellar material.
  vec3 fire=mix(cool,warm,clamp(d.y*.6+float(uVariant==1)*.4,0.,1.));
  float opacity=clamp(d.x*ds*.6,0.,.7);
  c+=fire*opacity*transmission*(1.1+band*.65+uBass*.22+uEvent*.35);
  transmission*=1.-opacity*.38;
 }
 vec2 q=rd.xy/max(.18,abs(rd.z));
 float clouds=fbm(q*2.+vec2(uChapterTime*.015,0.))*fbm(q*5.);
 c+=accent(q.y)*clouds*.05*transmission;
 vec2 cell=floor(q*240.);float h=hash(cell);c+=vec3(.35,.55,.9)*step(.998,h)*transmission;
 if(uVariant==0){
  // An expanding full-frame halo is the opening action, with a visible start and finish.
  float wave=length(p)-(.2+k*2.3);c+=vec3(.14,.5,1.)*line(wave,.025)*sin(k*PI)*.45;
 }
 color=vec4(c,1.);
}
