#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;uniform float uEnergy;
#include "common.glsl"
// Matter collapses into fragments; aftershock rebuilds a skyline of asymmetric spires.
float blockS(vec3 p,vec3 b){vec3 q=abs(p)-b;return length(max(q,0.))+min(max(q.x,max(q.y,q.z)),0.);}
float shell(vec3 p){
 float k=clamp(uChapterProgress,0.,1.),a=uAction;
 if(uVariant==0){
  vec3 q=p;q.xy=rot(k*.35)*q.xy;
  float radius=1.65-a*.9-k*.12;
  vec3 g=q*3.3;float gyroid=(abs(dot(sin(g),cos(g.zxy)))-.24)/6.7;
  float d=max(gyroid,length(q)-radius);
  for(int i=0;i<6;i++){
   float angle=float(i)*PI/3.;vec3 offset=vec3(cos(angle),.4*sin(angle*2.),sin(angle))*(1.3+a*1.5);
   vec3 shard=q-offset;shard.xy=rot(angle+a)*shard.xy;
   float size=.12+a*.2;d=min(d,blockS(shard,vec3(size,.33+a*.18,size)));
  }
  return d;
 }
 if(uVariant==1){
  float d=100.;
  for(int i=0;i<5;i++){
   float fi=float(i),angle=fi*2.39996;float growth=.35+k*.2+a*.65;
   float h=(1.8+mod(fi*1.7,2.4))*growth;
   vec3 q=p-vec3(cos(angle)*(1.+fi*.18),h*.5-1.6,sin(angle)*(1.+fi*.18));
   q.xz=rot(angle*.3)*q.xz;
   float spire=blockS(q,vec3(.23+fi*.035,h*.5,.28));
   // Horizontal gaps distinguish rebuilt slabs from the collapsing porous sphere.
   spire=max(spire,-(abs(mod(q.y+.23,.46)-.23)-.045));d=min(d,spire);
  }
  return d;
 }
 if(uVariant==2){
  vec3 q=p;q.xz=rot(p.y*1.1-k*.8)*q.xz;
  float twin=length(vec2(abs(q.x)-(.65+a*.5),q.z))-.16;
  return max(twin,abs(p.y)-2.2);
 }
 float d=100.;
 for(int i=0;i<5;i++){
  vec3 q=p;float angle=float(i)*2.*PI/5.;q.xz=rot(angle)*q.xz;
  q-=vec3(.3+a*1.2,.1,0);q.xy=rot(.3+a*.8)*q.xy;
  float petal=(abs(q.x)+abs(q.y)*.6+abs(q.z)*1.8-1.0)*.45;
  d=min(d,petal);
 }
 return d;
}
vec2 mapS(vec3 p){float d=shell(p);return d<p.y+1.8?vec2(d,2):vec2(p.y+1.8,1);}
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
 float k=clamp(uChapterProgress,0.,1.);
 vec3 ro=uVariant==0?vec3(3.7-k*.8,1.5+k*.5,4.2):vec3(4.7-k*1.3,2.6+k,5.2);
 vec3 rd=cameraBasis(ro,vec3(0,uVariant==1?.1:0.,0))*normalize(vec3(p,1.7));
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
