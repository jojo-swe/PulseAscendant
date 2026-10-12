#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;
#include "common.glsl"
float box(vec3 p,vec3 b){vec3 q=abs(p)-b;return length(max(q,0.))+min(max(q.x,max(q.y,q.z)),0.);}
vec2 fighter(vec3 q,float wing){
 float body=box(q,vec3(.30,.18,1.45));
 vec3 w=q+vec3(0,.07,.2);w.xy=rot(sign(w.x)*wing*.25)*w.xy;
 float wings=box(w,vec3(.8+wing*.65,.055,.5));
 float cockpit=length((q-vec3(0,.22,.6))*vec3(1,1,.65))-.25;
 float ship=min(min(body,wings),cockpit);
 float engine=max(length(q.xy)-.15,abs(q.z+2.65)-1.2);
 return engine<ship?vec2(engine,2.):vec2(ship,1.);
}
vec3 pursuitTarget(){return vec3(sin(uChapterProgress*PI)*2.,.5+uAction*1.8,uChapterTime*15.+13.);}
vec3 fleetCell(vec3 p){
 if(uVariant==3)return p-pursuitTarget();
 if(uVariant==1)return p-vec3(0,0,28.);
 float spacing=uVariant==2?22.:18.;
 p.z=mod(p.z+spacing*.5,spacing)-spacing*.5;
 p.x=abs(mod(p.x+8.,16.)-8.)-(uVariant==2?4.5:4.+uAction*1.5);
 p.y=mod(p.y+5.,10.)-5.;return p;
}
vec2 fleet(vec3 p){
 float k=clamp(uChapterProgress,0.,1.);vec2 hit=vec2(100.,1.);
 if(uVariant==0){
  // Escorts spread from a narrow formation to reveal the flagship behind them.
  hit=fighter(fleetCell(p),.2+uAction*.8);
  vec3 q=p-vec3(0,-5.,45.);
  float carrier=box(q,vec3(6.,.9,10.));carrier=min(carrier,box(q-vec3(0,.8,-1.),vec3(2.2,1.4,3.)));
  if(carrier<hit.x)hit=vec2(carrier,3.);
 }else if(uVariant==1){
  // A close flyby of one immense, asymmetric mothership changes the scale completely.
  vec3 q=p-vec3(0,0,28.);
  float carrier=box(q,vec3(8.,1.15,17.));
  carrier=min(carrier,box(q-vec3(-2.,1.7,-5.),vec3(2.,1.4,5.)));
  carrier=min(carrier,box(q-vec3(5.,2.7,2.),vec3(.5,3.6,7.)));
  carrier=min(carrier,box(q-vec3(-9.,-.3,3.),vec3(3.,.35,9.)));
  hit=vec2(carrier,3.);
  float thrust=box(q-vec3(0,0,-18.5),vec3(5.,.5,1.5+uAction*2.));
  if(thrust<hit.x)hit=vec2(thrust,2.);
  vec3 escort=p-vec3(11.+uAction*3.,4.,24.+k*18.);
  vec2 small=fighter(escort,.7);if(small.x<hit.x)hit=small;
 }else if(uVariant==2){
  // Hangar launch: walls slide aside and fighters leave a physical interior.
  vec3 q=fleetCell(p);q.y-=uAction*2.;hit=fighter(q,.3+uAction*.7);
  float wall=abs(abs(p.x)-(8.+uAction*5.))-.25;
  float floorD=p.y+3.;if(min(wall,floorD)<hit.x)hit=vec2(min(wall,floorD),3.);
  vec3 strut=vec3(abs(p.x)-(7.6+uAction*5.),p.y,mod(p.z+7.,14.)-7.);
  float rib=box(strut,vec3(.45,5.,.22));if(rib<hit.x)hit=vec2(rib,1.);
  float roof=box(vec3(p.x,p.y-5.-uAction*4.,mod(p.z+7.,14.)-7.),vec3(8.+uAction*5.,.2,.3));
  if(roof<hit.x)hit=vec2(roof,3.);
 }else{
  // A finite chase follows one fighter through a spreading debris field.
  hit=fighter(p-pursuitTarget(),.85);
  vec3 q=p-pursuitTarget()-vec3(-5.-uAction*2.,1.,7.);vec2 escort=fighter(q,.6);
  if(escort.x<hit.x)hit=escort;
  vec3 debris=p;float cell=floor((p.z+10.)/20.);debris.z=mod(p.z+10.,20.)-10.;
  debris.xy-=vec2(sin(cell*2.3),cos(cell*1.7))*(5.+uAction*2.);
  debris.xy=rot(cell*.8+k)*debris.xy;
  float block=box(debris,vec3(.5,.7,1.1));if(block<hit.x)hit=vec2(block,3.);
  // A volley is fired once on the musical action; the bolts travel out of view.
  vec3 bolt=p-pursuitTarget()-vec3(2.,-.6,-8.+uAction*60.);
  float laser=max(length(bolt.xy)-.055,abs(bolt.z)-2.5);
  if(laser<hit.x)hit=vec2(laser,2.);
 }
 return hit;
}
vec3 normalAt(vec3 p){vec2 e=vec2(.005,0);return normalize(vec3(fleet(p+e.xyy).x-fleet(p-e.xyy).x,fleet(p+e.yxy).x-fleet(p-e.yxy).x,fleet(p+e.yyx).x-fleet(p-e.yyx).x));}
void main(){
 vec2 p=(vUV*2.-1.)*vec2(uResolution.x/uResolution.y,1.);float k=clamp(uChapterProgress,0.,1.);
 vec3 ro,target;
 if(uVariant==0){ro=vec3(.5,1.8,uChapterTime*5.);target=ro+vec3(0,-1.,22.);}
 else if(uVariant==1){ro=vec3(18.-k*3.,7.-k*2.,-5.+k*43.);target=vec3(0,1.,28.+k*9.);}
 else if(uVariant==2){ro=vec3(0,.7,uChapterTime*10.);target=ro+vec3(0,.3,20.);}
 else{ro=pursuitTarget()+vec3(2.7-k*1.2,1.3,-12.);target=pursuitTarget()+vec3(0,0,3.);p=rot(sin(k*PI)*.12)*p;}
 vec3 rd=cameraBasis(ro,target)*normalize(vec3(p,1.35));
 float t=.03,glow=0.;vec2 h;bool found=false;
 for(int i=0;i<128;i++){h=fleet(ro+rd*t);float ds=clamp(h.x*.75,.008,1.5);if(h.y>1.5&&h.y<2.5)glow+=exp(-abs(h.x)*12.)*ds;if(h.x<.004){found=true;break;}t+=ds;if(t>125.)break;}
 vec3 neon=accent(float(uVariant)*.6+.3);
 vec3 c=vec3(.004,.008,.02)+neon*fbm(rd.xy*3.)*.03;
 // Planet silhouettes add a horizon to the large-scale mothership pass.
 if(uVariant==1){vec3 planetDir=normalize(vec3(-.4,.25,1.));float d=length(rd-planetDir);
  c+=vec3(.04,.12,.25)*line(d-.32,.016);if(d<.30)c*=.3;}
 if(found){
  vec3 at=ro+rd*t,n=normalAt(at),q=fleetCell(at),l=normalize(vec3(-2,3,-1));
  float spec=pow(max(dot(reflect(-l,n),-rd),0.),50.),edge=pow(1.-max(dot(n,-rd),0.),3.);
  c=vec3(.13,.20,.30)*(.3+max(dot(n,l),0.))+vec3(.4,.7,1.)*spec;
  c+=neon*(edge*.45+line(abs(q.x)-.3,.015)*.3+line(q.z-.8,.02)*.3);
  if(h.y>2.5){vec2 panel=abs(fract(at.xz*.8)-.5);c+=neon*line(min(panel.x,panel.y),.025)*.16;}
  else if(h.y>1.5)c=neon*(1.2+uBass*.5+uEvent*.6)*2.;
  c=mix(c,vec3(.007,.012,.03),1.-exp(-t*.014));
 }
 c+=neon*glow*.10;color=vec4(c,1.);
}
