#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;
#include "common.glsl"
float box(vec3 p,vec3 b){vec3 q=abs(p)-b;return length(max(q,0.))+min(max(q.x,max(q.y,q.z)),0.);}
// Chamfered wedge hull, stepped flight deck, recessed hangar and twin engine pods.
float carrierHull(vec3 q){
 float hull=box(q,vec3(6.2,1.15,15.));
 hull=max(hull,(abs(q.x)+max(q.z,0.)*.32-6.2)*.9);
 hull=max(hull,(abs(q.x)*.45-q.y-3.4)*.9);
 float deck=box(q-vec3(0,1.4,-1.8),vec3(4.7,.28,10.8));
 deck=max(deck,(abs(q.x)+max(q.z-3.,0.)*.38-4.7)*.9);
 hull=min(hull,deck);
 hull=min(hull,box(q-vec3(-2.3,2.5,-5.),vec3(1.3,.85,3.7)));
 hull=min(hull,box(q-vec3(-2.3,3.7,-5.8),vec3(.95,.40,1.8)));
 hull=min(hull,box(q-vec3(-2.3,5.,-6.3),vec3(.075,1.,.075)));
 hull=min(hull,box(q-vec3(-2.3,4.8,-6.3),vec3(.9,.055,.06)));
 vec3 bay=vec3(abs(q.x)-5.9,q.y-.05,q.z+1.5);
 hull=max(hull,-box(bay,vec3(1.5,.62,4.0)));
 float pod=max(length(vec2(abs(q.x)-5.3,q.y+.2))-1.2,abs(q.z+11.5)-4.0);
 hull=min(hull,pod);
 // Large ribs around the pods establish readable industrial scale.
 float ribZ=mod(q.z+12.,2.5)-1.25;
 float ribs=max(length(vec2(abs(q.x)-5.3,q.y+.2))-1.3,abs(ribZ)-.12);
 ribs=max(ribs,abs(q.z+11.5)-3.7);hull=min(hull,ribs);
 return hull;
}
vec2 fighter(vec3 q,float wing){
 float body=box(q,vec3(.32,.19,1.45));
 body=max(body,(abs(q.x)+max(q.z,0.)*.19-.32)*.97);
 vec3 w=q+vec3(0,.07,.2);w.xy=rot(sign(w.x)*wing*.25)*w.xy;
 float wings=box(w,vec3(.8+wing*.65,.055,.65));
 wings=max(wings,(abs(w.x)*.5+w.z-.95)*.85);
 float cockpit=length((q-vec3(0,.22,.4))*vec3(1,1,.65))-.25;
 float ship=min(min(body,wings),cockpit);
 vec3 fin=q-vec3(0,.35,-.75);fin.yz=rot(-.25)*fin.yz;
 ship=min(ship,box(fin,vec3(.04,.35,.35)));
 float engine=max(length(q.xy)-(.10+uPulse.x*.04),abs(q.z+2.25)-(.65+uPulse.x*.5));
 return engine<ship?vec2(engine,2.):vec2(ship,1.);
}
vec3 flagshipTarget(){return vec3(1.8*sin(uChapterProgress*PI),-4.+uChapterProgress*1.5,uChapterTime*9.+34.-uChapterProgress*9.);}
vec3 pursuitTarget(){return vec3(sin(uChapterProgress*PI)*2.,.5+uAction*1.8,uChapterTime*23.+13.);}
vec3 fleetCell(vec3 p){
 if(uVariant==3)return p-pursuitTarget();
 if(uVariant==1)return p-vec3(0,0,28.);
 float spacing=uVariant==2?30.:34.;
 p.z=mod(p.z+spacing*.5,spacing)-spacing*.5;
 p.x=abs(mod(p.x+14.,28.)-14.)-(uVariant==2?4.5:7.+uAction*2.);
 p.y=mod(p.y+10.,20.)-10.;
 p.y-=sin(uChapterTime*1.25+p.z*.12)*.16;
 p.x-=sin(uChapterTime*.9+p.z*.14)*.18;return p;
}
vec2 fleet(vec3 p){
 float k=clamp(uChapterProgress,0.,1.);vec2 hit=vec2(100.,1.);
 if(uVariant==0){
  // Escorts spread from a narrow formation to reveal the flagship behind them.
  hit=fighter(fleetCell(p),.2+uAction*.8);
  // A finite escort band replaces the wall of repeated luminous confetti.
  hit.x=max(hit.x,max(abs(p.x)-35.,abs(p.y)-4.));
  vec3 q=p-flagshipTarget();
  float carrier=carrierHull(q);
  if(carrier<hit.x)hit=vec2(carrier,3.);
  vec3 exhaust=vec3(abs(q.x)-5.3,q.y+.2,q.z+17.);
  float thrust=max(length(exhaust.xy)-.7,abs(exhaust.z)-(1.1+uPulse.x));
  if(thrust<hit.x)hit=vec2(thrust,2.);
  // Two close escorts overtake the tracked flagship throughout the reveal.
  for(int i=0;i<2;i++){
   float side=i==0?-1.:1.;
   vec3 escort=p-flagshipTarget()-vec3(side*(8.5-uAction*1.3),5.+sin(uChapterTime*.8+side)*.5,-8.+k*13.);
   vec2 small=fighter(escort,.75);if(small.x<hit.x)hit=small;
  }
 }else if(uVariant==1){
  // A close flyby of one immense, asymmetric mothership changes the scale completely.
  vec3 q=p-vec3(0,0,28.);
  float carrier=carrierHull(q);
  hit=vec2(carrier,3.);
  vec3 exhaust=vec3(abs(q.x)-5.3,q.y+.2,q.z+17.);
  float thrust=max(length(exhaust.xy)-.78,abs(exhaust.z)-(1.4+uPulse.x*1.3));
  if(thrust<hit.x)hit=vec2(thrust,2.);
  vec3 escort=p-vec3(11.+uAction*3.,4.+sin(uChapterTime*1.2)*.4,24.+k*25.);
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
  debris.xy=rot(cell*.8+k+uChapterTime*.8)*debris.xy;
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
 if(uVariant==0){ro=vec3(-3.+k*6.,2.8,uChapterTime*9.);target=flagshipTarget()+vec3(0,2.,2.);}
 else if(uVariant==1){ro=vec3(21.-k*4.,9.-k*2.,-9.+k*36.);target=vec3(0,1.,26.+k*5.);}
 else if(uVariant==2){ro=vec3(0,.7,uChapterTime*16.);target=ro+vec3(0,.3,20.);}
 else{ro=pursuitTarget()+vec3(1.2-k*.4,.8,-6.);target=pursuitTarget()+vec3(0,0,3.);p=rot(sin(k*PI)*.12)*p;}
 vec3 rd=cameraBasis(ro,target)*normalize(vec3(p,1.35));
 float t=.03,glow=0.;vec2 h;bool found=false;
 for(int i=0;i<128;i++){h=fleet(ro+rd*t);float ds=clamp(h.x*.75,.008,1.5);if(h.y>1.5&&h.y<2.5)glow+=exp(-abs(h.x)*12.)*ds;if(h.x<.004){found=true;break;}t+=ds;if(t>125.)break;}
 vec3 neon=vec3(.035,.40,.70);
 vec3 c=vec3(.004,.008,.02)+neon*fbm(rd.xy*3.)*.03;
 // Planet silhouettes add a horizon to the large-scale mothership pass.
 if(uVariant==1){vec3 planetDir=normalize(vec3(-.4,.25,1.));float d=length(rd-planetDir);
  c+=vec3(.04,.12,.25)*line(d-.32,.016);if(d<.30)c*=.3;}
 if(found){
  vec3 at=ro+rd*t,n=normalAt(at),q=fleetCell(at),l=normalize(vec3(-2,3,-1));
  if(uVariant==0&&h.y>2.5)q=at-flagshipTarget();
  float spec=pow(max(dot(reflect(-l,n),-rd),0.),50.),edge=pow(1.-max(dot(n,-rd),0.),3.);
  float plates=step(.035,fract(at.x*1.6))*step(.035,fract(at.z*.6));
  float machining=.93+.07*sin(at.z*100.+at.x*65.);
  c=vec3(.10,.16,.24)*(.3+max(dot(n,l),0.))*(.55+.45*plates)*machining+vec3(.4,.7,1.)*spec;
  c+=neon*(edge*.16+line(abs(q.x)-.3,.015)*.3+line(q.z-.8,.02)*.3);
  if(h.y>2.5){
   vec2 uv=abs(n.y)>.5?at.xz:vec2(abs(n.x)>.5?at.z:at.x,at.y);
   vec2 panel=abs(fract(uv*.8)-.5);
   c*=.7+.3*smoothstep(.015,.06,min(panel.x,panel.y));
   float windows=(1.-abs(n.y))*step(.68,fract(uv.x*1.4))*step(.64,fract(uv.y*2.));
   windows*=step(.65,hash(floor(uv*vec2(1.4,2.))));
   c+=vec3(.6,.38,.16)*windows*(.15+uPulse.y*.28);
   float deckLine=line(abs(q.x)-2.6,.07)*step(.5,fract(q.z*.6))*max(n.y,0.);
   c+=vec3(.06,.48,.65)*deckLine*(.4+uPulse.x*.6);
   float beacon=pow(.5+.5*cos(at.z*1.2-uChapterTime*5.),16.);
   c+=vec3(1.,.26,.035)*line(abs(q.x)-5.3,.055)*beacon*(.15+uPulse.z*.3);

  }
  else if(h.y>1.5)c=neon*(.6+uBass*.5+uEvent*.6+uPulse.x*1.8)*1.5;
  c=mix(c,vec3(.007,.012,.03),1.-exp(-t*.014));
 }
 // World-space tracer lanes give moving foreground scale and remain depth occluded.
 for(int i=0;i<8;i++){
  float fi=float(i),depth=mod(fi*13.+uChapterTime*28.,104.);
  vec3 streak=vec3(ro.x+(mod(fi,2.)*2.-1.)*(3.5+mod(fi,3.)*1.5),ro.y+sin(fi*2.4)*3.,ro.z+depth);
  float along=dot(streak-ro,rd),miss=length(ro+rd*along-streak);
  if(along>0.&&(!found||along<t))c+=vec3(.12,.35,.65)*exp(-miss*22.)*(.25+uPulse.z*.7);
 }
 c+=neon*glow*(.10+uPulse.x*.10);color=vec4(c,1.);
}
