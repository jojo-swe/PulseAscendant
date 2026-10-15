#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;uniform float uEnergy;
uniform vec4 uPlanes[24];
#include "common.glsl"
mat3 crystalRotation(){
 float a=.3+uChapterProgress*.45+uAction*.35+uChapterTime*.22,b=.4+float(uVariant)*.57+uChapterTime*.12;
 float c=cos(a),s=sin(a),cb=cos(b),sb=sin(b);
 return mat3(cb,0,-sb,0,1,0,sb,0,cb)*mat3(1,0,0,0,c,s,0,-s,c);
}
vec3 crystalOffset(){
 if(uVariant==1)return vec3(0,.2,-.8);
 if(uVariant==2)return vec3(0,.45+uAction*.5,0);
 if(uVariant>=4)return vec3(0,.1+uAction*1.5,0);
 return vec3(0,.15,0);
}
vec3 crystalScale(){
 if(uVariant==1)return vec3(.35,.35,1.15)*(1.+uPulse.x*.065);
 if(uVariant==2)return vec3(.48,.75,.48)*(1.+uPulse.x*.065);
 if(uVariant==3)return vec3(.70,.90,.70)*(1.+uPulse.x*.065);
 return vec3(.55)*(1.+uPulse.x*.065);
}
// Exact convex clipping against the 24 planes derived from original GLENZ triangles.
bool crystalHit(vec3 ro,vec3 rd,out float nearT,out float farT,out vec3 nearN,out vec3 farN) {
 mat3 rotation=crystalRotation();
 vec3 scale=crystalScale();vec3 o=transpose(rotation)*(ro-crystalOffset())/scale,d=transpose(rotation)*rd/scale;
 nearT=-10000.;farT=10000.;nearN=vec3(0);farN=vec3(0);
 for(int i=0;i<24;i++) {
  vec3 n=uPlanes[i].xyz;float dist=uPlanes[i].w-dot(n,o),den=dot(n,d);
  if(abs(den)<.000001){if(dist<0.)return false;}
  else {
   float t=dist/den;
   if(den<0.&&t>nearT){nearT=t;nearN=normalize(rotation*(n/scale));}
   if(den>0.&&t<farT){farT=t;farN=normalize(rotation*(n/scale));}
  }
 }
 return farT>max(nearT,0.);
}
float blockE(vec3 p,vec3 b){vec3 q=abs(p)-b;return length(max(q,0.))+min(max(q.x,max(q.y,q.z)),0.);}
vec2 mapScene(vec3 p){
 float k=clamp(uChapterProgress,0.,1.),action=uAction;vec2 hit=vec2(p.y+1.65,1.);
 if(uVariant==0){
  // The reactor uncages its core: three massive gimbals unlock into different planes.
  for(int i=0;i<3;i++){
   vec3 q=p;float fi=float(i);q.xz=rot(fi*1.1+action*.5+uChapterTime*(.32+fi*.14))*q.xz;
   q.yz=rot(.25+fi*.7+action*(fi-1.)*.9+uChapterTime*.18)*q.yz;
   float radius=1.15+fi*.28+action*.15;
   float tube=length(vec2(length(q.xy)-radius,q.z))-.065;
   if(tube<hit.x)hit=vec2(tube,2.+fi);
  }
  float foot=blockE(vec3(abs(p.x)-2.,p.y+.8,p.z),vec3(.55,.85,.65));
  if(foot<hit.x)hit=vec2(foot,5.);
 }else if(uVariant==1){
  // A longitudinal rail cannon replaces the orbital rings entirely.
  float rails=blockE(vec3(abs(p.x)-1.15,p.y+.3,p.z),vec3(.14,.18,3.5));
  if(rails<hit.x)hit=vec2(rails,3.);
  for(int i=0;i<4;i++){
   float z=float(i)*1.7-2.5+sin(uChapterTime*2.4-float(i)*.7)*.09;vec3 q=p-vec3(0,0,z+action*.5);
   float collar=max(abs(q.x)*.866+abs(q.y)*.5,abs(q.y))-(1.35+action*.5);
   float hoop=length(vec2(collar,q.z))-.09;if(hoop<hit.x)hit=vec2(hoop,2.+mod(float(i),3.));
  }
 }else if(uVariant==2){
  // Exploded modules converge into a tall asymmetric prism cradle.
  for(int i=0;i<6;i++){
   float fi=float(i),a=fi*PI/3.+uChapterTime*.14,r=2.5-action*1.3+uPulse.x*.12;
   vec3 at=vec3(cos(a)*r,-.2+mod(fi,3.)*.45+action*.15,sin(a)*r);
   vec3 q=p-at;q.xz=rot(a)*q.xz;
   float d=blockE(q,vec3(.28,.35+fi*.05,.38));if(d<hit.x)hit=vec2(d,2.+mod(fi,3.));
  }
  float pedestal=blockE(p+vec3(0,1.45,0),vec3(.8,.2,.8));if(pedestal<hit.x)hit=vec2(pedestal,5.);
 }else if(uVariant==3){
  // A crown of blade-like cathedral buttresses opens around a taller crystal.
  for(int i=0;i<6;i++){
   float fi=float(i),a=fi*PI/3.;vec3 q=p;q.xz=rot(a)*q.xz;
   q-=vec3(1.65+action*.6,.2,0);q.xy=rot(.25+action*.35+sin(uChapterTime*1.7+fi)*.045+uPulse.y*.07)*q.xy;
   float fin=blockE(q,vec3(.13,1.8,.52));if(fin<hit.x)hit=vec2(fin,2.+mod(fi,3.));
  }
  float crown=length(vec2(length(p.xz)-(2.1+action*.5),p.y+1.1))-.08;
  if(crown<hit.x)hit=vec2(crown,4.);
 }else{
  // The Act I finale: four separated decks align into a launch spire.
  for(int i=0;i<4;i++){
   float fi=float(i),y=-1.2+fi*.67;vec3 q=p-vec3(0,y,0);
   q.xz=rot((1.-action)*fi*.6+uChapterTime*(.18+fi*.035))*q.xz;
   float r=1.55-fi*.19+action*.15;
   float deck=blockE(q,vec3(r,.07,r));
   deck=max(deck,-(length(q.xz)-(.6+action*.32)));
   if(deck<hit.x)hit=vec2(deck,2.+mod(fi,3.));
  }
  vec3 b=p-vec3(0,.6+action*1.5,0);
  float beam=max(length(b.xz)-.055,abs(b.y)-(1.3+action*2.));if(beam<hit.x)hit=vec2(beam,6.);
 }
 return hit;
}
vec3 normalAt(vec3 p) {
 vec2 e=vec2(.002,0.);
 return normalize(vec3(mapScene(p+e.xyy).x-mapScene(p-e.xyy).x,mapScene(p+e.yxy).x-mapScene(p-e.yxy).x,mapScene(p+e.yyx).x-mapScene(p-e.yyx).x));
}
float shadow(vec3 p,vec3 l) {
 float res=1.,t=.035;
 float cn,cf;vec3 nn,fn;
 if(uRetro==0&&crystalHit(p,l,cn,cf,nn,fn)&&cn>0.&&cn<5.)res=.4;
 for(int i=0;i<24;i++) {float d=mapScene(p+l*t).x;res=min(res,12.*d/t);t+=clamp(d,.025,.3);if(d<.001||t>5.)break;}
 return clamp(res,.08,1.);
}
float ao(vec3 p,vec3 n) {
 float occ=0.,weight=1.;for(int i=1;i<=4;i++){float h=float(i)*.08;occ+=(h-mapScene(p+n*h).x)*weight;weight*=.55;}return clamp(1.-occ*2.,.2,1.);
}
vec3 sky(vec3 d) {
 vec3 c=mix(vec3(.012,.018,.04),vec3(.045,.09,.13),smoothstep(-.2,1.,d.y));
 c+=vec3(.2,.65,1.2)*pow(max(0.,dot(d,normalize(vec3(-3,2,1)))),28.);
 c+=vec3(1.2,.24,.07)*pow(max(0.,dot(d,normalize(vec3(3,3,2)))),35.);
 c+=vec3(.4,.5,.6)*pow(max(0.,dot(d,normalize(vec3(0,4,-2)))),90.);
 return c;
}
vec3 fresnel(float x,vec3 f0) {return f0+(1.-f0)*pow(1.-clamp(x,0.,1.),5.);}
vec3 light(vec3 n,vec3 v,vec3 l,vec3 base,float metal,float rough) {
 vec3 h=normalize(l+v);float nl=max(dot(n,l),0.),nv=max(dot(n,v),.01),nh=max(dot(n,h),0.);
 float a=rough*rough,a2=a*a,den=nh*nh*(a2-1.)+1.;float D=a2/(PI*den*den+.00001);
 float k=(rough+1.)*(rough+1.)/8.;float G=nv/(nv*(1.-k)+k)*nl/(nl*(1.-k)+k);
 vec3 F=fresnel(max(dot(h,v),0.),mix(vec3(.04),base,metal));
 return ((1.-F)*(1.-metal)*base/PI+D*G*F/(4.*nv*max(.01,nl)))*nl;
}
vec3 emission(float id,vec3 p) {
 vec3 c=id<3.?vec3(.025,.65,1.4):(id<4.?vec3(1.5,.18,.035):vec3(.25,.12,1.));
 if(id>=5.)c=mix(vec3(.02,.7,1.5),vec3(1.8,.22,.035),step(6.,id));
 float band=frequencyBand((atan(p.y,p.x)/PI+1.)*11.99);
 float runner=pow(.5+.5*cos(p.y*8.+p.z*5.-uChapterTime*8.+id),18.);
 float pulse=.22+band*1.3+uHit*.3+uPulse.x*1.7+runner*(.5+uPulse.z);
 return c*pulse;
}
vec3 shade(vec3 p,vec3 n,vec3 v,float id,bool reflection) {
 bool floorHit=id<1.5;
 vec3 base=floorHit?vec3(.028,.035,.043):vec3(.4,.48,.54);
 float rough=floorHit?.24:.19,metal=floorHit?.5:.92;
 float material=fbm(p.xz*9.);
 float machining=.92+.08*sin((p.x+p.z)*170.);
 base*=(.8+.4*material)*machining;
 vec3 l=normalize(vec3(3,4,2)-p),l2=normalize(vec3(-4,2,1)-p);
 float sh=reflection?1.:shadow(p+n*.015,l);
 vec3 c=light(n,v,l,base,metal,rough)*vec3(7.,3.,1.5)*sh;
 c+=light(n,v,l2,base,metal,rough)*vec3(1.,4.,7.);
 c+=sky(reflect(-v,n))*fresnel(max(dot(n,v),0.),mix(vec3(.04),base,metal))*.4;
 c+=base*.08*ao(p,n);
 if(!floorHit)c+=emission(id,p)*(.04+pow(.5+.5*sin(p.y*22.+p.x*11.),12.)*.5);
 if(floorHit) {
  vec2 grid=abs(fract(p.xz*.7)-.5);float lineG=1.-smoothstep(.0,.012,min(grid.x,grid.y));
  float conduit=exp(-abs(p.x)*8.)+exp(-abs(p.z)*8.);
  c+=vec3(.015,.08,.1)*lineG;
  c+=vec3(.02,.3,.5)*conduit*pow(.5+.5*cos(length(p.xz)*6.-uChapterTime*10.),14.)*(.2+uPulse.x);
 }
 return c;
}
vec3 traceReflection(vec3 ro,vec3 rd) {
 float t=.025;
 float cn,cf;vec3 nn,fn;
 bool glass=uRetro==0&&crystalHit(ro,rd,cn,cf,nn,fn)&&cn>0.;
 for(int i=0;i<64;i++) {
  // A bounded approximation for the crystal in secondary floor reflections.
  if(glass&&t>=cn)return sky(reflect(rd,nn))*.65+vec3(.02,.1,.13);
  vec3 p=ro+rd*t;vec2 d=mapScene(p);
  if(d.x<.003)return shade(p,normalAt(p),-rd,d.y,true);
  t+=max(.008,d.x*.85);if(t>12.)break;
 }
 return sky(rd);
}
vec3 transmitted(vec3 p,vec3 rd,vec3 n,float ior) {
 vec3 inside=refract(rd,n,1./ior);
 float tn,tf;vec3 nn,nf;
 if(!crystalHit(p+inside*.002,inside,tn,tf,nn,nf))return sky(rd);
 vec3 exitP=p+inside*(tf+.002),outside=refract(inside,-nf,ior);
 // One internal reflection for total internal reflection, with a strict bounce bound.
 if(dot(outside,outside)<.001) {
  inside=reflect(inside,-nf);exitP+=inside*.004;
  if(!crystalHit(exitP,inside,tn,tf,nn,nf))return sky(rd);
  exitP+=inside*(tf+.002);outside=refract(inside,-nf,ior);
  if(dot(outside,outside)<.001)return sky(reflect(rd,n));
 }
 vec3 beer=exp(-vec3(.17,.055,.025)*tf);
 return traceReflection(exitP+outside*.005,outside)*beer;
}
vec3 glassSurface(vec3 p,vec3 rd,vec3 n) {
 vec3 reflected=traceReflection(p+n*.008,reflect(rd,n));
 // Two wavelengths approximate dispersion while keeping secondary work bounded.
 vec3 red=transmitted(p,rd,n,1.45),blue=transmitted(p,rd,n,1.48);
 vec3 through=vec3(red.r,(red.g+blue.g)*.5,blue.b);
 float F=.04+.96*pow(1.-max(dot(-rd,n),0.),5.);
 vec3 c=mix(through,reflected,F);
 vec3 local=transpose(crystalRotation())*(p-crystalOffset())/crystalScale();
 float first=100.,second=100.;
 for(int i=0;i<24;i++) {
  float d=abs(dot(uPlanes[i].xyz,local)-uPlanes[i].w);
  if(d<first){second=first;first=d;}else second=min(second,d);
 }
 float edge=exp(-second*130.);
 c+=mix(vec3(.02,.6,1.3),vec3(1.1,.1,.3),.5+.5*sin(p.y*2.+uTime*.4))*edge*(.35+uEnergy+uPulse.x*1.8+uPulse.z*.7);
 c+=vec3(.025,.045,.065)*(.4+F);
 return c;
}
void main() {
 vec2 p=(vUV*2.-1.)*vec2(uResolution.x/uResolution.y,1.);
 float k=clamp(uChapterProgress,0.,1.);
 vec3 ro;
 if(uVariant==1)ro=vec3(4.7-k,2.3,5.7-k*1.8);
 else if(uVariant==2)ro=vec3(4.6-k*.5,2.7+k*.5,4.6);
 else if(uVariant==3)ro=vec3(4.7,2.3+k,5.4-k*.7);
 else if(uVariant>=4)ro=vec3(4.5-k*.6,2.8+k*.4,5.5);
 else ro=vec3(4.0-k*.6,1.7+k*.3,4.4);
 vec3 target=vec3(0,uVariant>=4?.8:0.,0);float lens=1.7;
 cinematicRig(ro,target,p,lens,1.,.55);
 vec3 rd=cameraBasis(ro,target)*normalize(vec3(p,lens));
 float t=0.,glow=0.;vec2 hit=vec2(0);bool found=false;
 int steps=uRetro==1?64:112;
 for(int i=0;i<112;i++) {
  if(i>=steps)break;
  vec3 pos=ro+rd*t;hit=mapScene(pos);
  float dt=clamp(hit.x*.8,.004,.5);
  if(hit.y>1.5)glow+=exp(-abs(hit.x)*16.)*dt;
  if(hit.x<.002){found=true;break;}t+=dt;if(t>18.)break;
 }
 vec3 c=sky(rd)*.18;
 float cn,cf;vec3 nn,fn;
 bool glass=uRetro==0&&crystalHit(ro,rd,cn,cf,nn,fn)&&cn>0.&&(!found||cn<t);
 if(glass)c=glassSurface(ro+rd*cn,rd,nn);
 else if(found) {
  vec3 pos=ro+rd*t,n=normalAt(pos);c=shade(pos,n,-rd,hit.y,false);
  if(hit.y<1.5&&uRetro==0) {
   n=normalize(n+vec3(sin(pos.x*14.+uTime*.4),0,cos(pos.z*11.-uTime*.3))*.014);
   vec3 refl=traceReflection(pos+n*.02,reflect(rd,n));
   float F=.25+.6*pow(1.-max(dot(n,-rd),0.),5.);
   c=mix(c,refl,F);
  }
  c=mix(c,sky(rd)*.08,1.-exp(-t*(hit.y<1.5?.14:.035)));
 }
 // Integrated emissive scattering along the primary ray, rather than a screen-space halo.
 c+=vec3(.025,.35,.65)*glow*(.4+uEnergy*.5+uPulse.x*.8);
 color=vec4(c,1.);
}
