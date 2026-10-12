#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;
#include "common.glsl"
float boxO(vec3 p,vec3 b){vec3 q=abs(p)-b;return length(max(q,0.))+min(max(q.x,max(q.y,q.z)),0.);}
float waves(vec2 p){
 float calm=uVariant==1?.35:.7;
 float swell=sin(p.x*.34+uTime*.35)*.16+sin(p.y*.27-uTime*.28)*.22;
 float detail=sin(dot(p,vec2(.7,.5))+uTime*.45)*(.03+midrange()*.07);
 return (swell+detail)*calm;
}
vec3 sky(vec3 rd){
 // The garden is lit from behind the camera; its composition is foliage against sky.
 vec3 sunDir=uVariant==2?normalize(vec3(.9,.45,1.)):normalize(vec3(uVariant==0?.35:-.25,.13,1.));
 float sun=pow(max(dot(rd,sunDir),0.),240.);
 vec3 horizon=uVariant==0?vec3(.16,.055,.19):(uVariant==1?vec3(.025,.08,.15):vec3(.32,.17,.09));
 vec3 zenith=uVariant==2?vec3(.025,.07,.12):vec3(.005,.015,.05);
 vec3 c=mix(zenith,horizon,exp(-abs(rd.y)*5.));c+=vec3(1.7,.6,.17)*sun;
 float aurora=pow(.5+.5*sin(rd.x*12.+rd.y*8.+sin(rd.x*9.+uTime*.04)),15.);
 if(uVariant==1)c+=vec3(.025,.28,.22)*aurora*.18*smoothstep(.03,.4,rd.y);
 return c;
}
float crystalReed(vec3 q,float h,float reveal){
 float trunk=max(length(q.xz)-(.24+.045*(h-q.y)),abs(q.y-h*.5)-h*.5);
 for(int branch=0;branch<2;branch++){
  float side=branch==0?-1.:1.;
  vec3 leaf=q-vec3(side*(1.3+reveal*.6),h*.60,0.);
  leaf.xy=rot(-side*(.42+reveal*.15))*leaf.xy;
  float crystal=(abs(leaf.x)*.70+abs(leaf.y)*.24+abs(leaf.z)*1.3-1.0)*.55;
  trunk=min(trunk,crystal);
 }
 return trunk;
}
vec2 scene(vec3 p){
 float k=clamp(uChapterProgress,0.,1.),water=(p.y-waves(p.xz))*.6;
 float reveal=.2+.3*k+uAction*.7,object=100.;
 if(uVariant==0){
  // Open sea remains the establishing contrast; it is no longer every sea chapter.
  vec2 cell=floor((p.xz+9.)/18.),q=mod(p.xz+9.,18.)-9.;float seed=hash(cell);
  if(abs(cell.x)>=1.){float h=2.+seed*5.;object=boxO(vec3(q.x,p.y-h*reveal*.5,q.y),vec3(.32,h*reveal*.5,.65));}
 }else if(uVariant==1){
  // Three finite, room-sized islands fill the aerial view, rather than dotting a horizon.
  for(int island=0;island<3;island++){
   float fi=float(island),lift=4.+uAction*5.+k+fi*.55;
   vec3 centre=island==0?vec3(0,lift,0):(island==1?vec3(-10.,lift,-5.):vec3(10.,lift,6.));
   vec3 q=p-centre;q.xz=rot(fi*.24+uAction*.14)*q.xz;
   vec2 size=island==0?vec2(5.8,4.3):vec2(3.6,3.);
   float plate=boxO(q,vec3(size.x,.55,size.y));
   // A deep visible fracture separates each broad tablet into two halves.
   vec3 crack=q;crack.xz=rot(.25)*crack.xz;
   plate=max(plate,-boxO(crack,vec3(.16+uAction*.18,1.,6.)));
   float keel=max((abs(q.x)+abs(q.z)-(size.x-.6+q.y*.7))*.55,max(q.y+.55,-q.y-4.2));
   object=min(object,min(plate,keel));
   if(island==0){
    float spires=boxO(vec3(abs(q.x)-3.1,q.y-2.5,q.z),vec3(.32,2.,.45));object=min(object,spires);
   }
  }
 }else{
  // Hero reeds are close and taller than the camera. The entire view reads as a garden.
  float h=6.4+reveal*4.1;
  object=crystalReed(p,h,reveal);
  object=min(object,crystalReed(p-vec3(-4.7,0,-3.2),h*.83,reveal));
  object=min(object,crystalReed(p-vec3(4.5,0,-4.5),h*.92,reveal));
 }
 return water<object?vec2(water,1.):vec2(object,2.);
}
vec3 normalAt(vec3 p){vec2 e=vec2(.012,0);return normalize(vec3(scene(p+e.xyy).x-scene(p-e.xyy).x,scene(p+e.yxy).x-scene(p-e.yxy).x,scene(p+e.yyx).x-scene(p-e.yyx).x));}
void main(){
 vec2 p=(vUV*2.-1.)*vec2(uResolution.x/uResolution.y,1.);float k=clamp(uChapterProgress,0.,1.);
 // Each return has a different dominant composition and a clear, finite camera path.
 vec3 ro,target;float lens=1.5;
 if(uVariant==0){
  ro=vec3(sin(k*PI)*.8,2.3+k*.65,uChapterTime*4.+4.);
  target=ro+vec3(1.5,-.8,20.);
 }else if(uVariant==1){
  // Safely above all island geometry, looking down across a large central split island.
  ro=vec3(12.-k*8.,22.+k*2.,14.-k*5.);
  target=vec3(-1.,4.+uAction*5.,0.);lens=1.7;
 }else{
  // A lateral dolly in the unobstructed foreground, looking up into branching crowns.
  ro=vec3(8.-k*14.,3.4+k*.5,13.-k*2.);
  target=vec3(0,6.9+uAction*.6,-1.);lens=1.35;
 }
 vec3 rd=cameraBasis(ro,target)*normalize(vec3(p,lens));
 float t=.02;vec2 h;bool found=false;
 for(int i=0;i<112;i++){h=scene(ro+rd*t);if(h.x<.006){found=true;break;}t+=clamp(h.x,.008,2.);if(t>160.)break;}
 vec3 c=sky(rd);
 if(found){
  vec3 at=ro+rd*t,n=normalAt(at);
  if(h.y>1.5){
   vec3 l=normalize(vec3(-.3,.8,.5));float edge=pow(1.-max(dot(n,-rd),0.),3.);
   c=vec3(.035,.12,.15)*(.35+max(dot(n,l),0.))+sky(reflect(rd,n))*.7;
   c+=mix(vec3(.03,.6,.8),vec3(.4,.8,.2),float(uVariant==2))*(edge*.45+uEvent*.2);
   if(uVariant==0)c+=vec3(.03,.3,.55)*line(at.y-(uChapterProgress*7.),.08);
  }else{
   float fres=.08+.92*pow(1.-max(dot(n,-rd),0.),5.);
   c=mix(vec3(.005,.035,.055),sky(reflect(rd,n)),fres);
   float foam=pow(.5+.5*sin(at.x*.7+at.z*.5+waves(at.xz)*5.-uTime*.3),22.);
   // One expanding wave marks each chapter's musical event and then leaves the frame.
   float waveRadius=uAction*70.+uChapterProgress*4.;
   float ring=line(length(at.xz-vec2(0,32.))-waveRadius,.3);
   c+=vec3(.04,.24,.3)*(foam*.12+ring*.6)*(1.+uBass*.2);
  }
  c=mix(c,sky(rd),1.-exp(-t*.009));
 }
 color=vec4(c,1.);
}
