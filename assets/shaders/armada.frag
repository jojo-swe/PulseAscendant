#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;
#include "common.glsl"
float box(vec3 p,vec3 b){vec3 q=abs(p)-b;return length(max(q,0.))+min(max(q.x,max(q.y,q.z)),0.);}
float smin(float a,float b,float k){float h=clamp(.5+.5*(b-a)/k,0.,1.);return mix(b,a,h)-k*h*(1.-h);}
float capsule(vec3 p,vec3 a,vec3 b,float r){vec3 ab=b-a;float f=clamp(dot(p-a,ab)/dot(ab,ab),0.,1.);return length(p-a-ab*f)-r;}
// Tapered capsule between two points; conservative for gentle tapers.
float roundCone(vec3 p,vec3 a,vec3 b,float ra,float rb){vec3 ab=b-a;float f=clamp(dot(p-a,ab)/dot(ab,ab),0.,1.);return (length(p-a-ab*f)-mix(ra,rb,f))*.9;}
float ellipsoid(vec3 p,vec3 r){return (length(p/r)-1.)*min(r.x,min(r.y,r.z));}
// Two exhaust pairs: outboard pods and an inner pair under the flight deck.
float carrierExhaust(vec3 q,float radius,float length_){
 vec3 outer=vec3(abs(q.x)-5.3,q.y+.2,q.z+17.),inner=vec3(abs(q.x)-2.1,q.y+.55,q.z+16.6);
 return min(max(length(outer.xy)-radius,abs(outer.z)-length_),max(length(inner.xy)-radius*.62,abs(inner.z)-length_*.7));
}
// Capital ship: chamfered wedge hull with a split ram prow, armour belts and
// recessed trenches, a stepped flight deck with turret rows, a layered command
// tower with sensor dish and masts, and four engine housings with nozzle rings.
float carrierHull(vec3 q){
 float bound=box(q,vec3(7.6,7.2,18.5));if(bound>1.)return bound;
 float hull=box(q,vec3(6.2,1.15,15.));
 hull=max(hull,(abs(q.x)+max(q.z,0.)*.32-6.2)*.9);
 hull=max(hull,(abs(q.x)*.45-q.y-3.4)*.9);
 // Ventral keel and a notched ram prow.
 hull=smin(hull,max(box(q-vec3(0,-1.25,1.),vec3(2.2,.5,12.5)),(abs(q.x)+max(q.z-4.,0.)*.25-2.2)*.9),.3);
 hull=max(hull,-box(vec3(q.x,q.y+.1,q.z-15.),vec3(.35,.5,2.4)));
 // Armour belts along both flanks with recessed trenches between them.
 vec3 belt=vec3(abs(q.x)-6.,q.y+.15,q.z+2.);
 hull=min(hull,max(box(belt,vec3(.35,.38,11.)),(abs(q.x)+max(q.z,0.)*.32-6.45)*.9));
 vec3 trench=vec3(abs(q.x)-6.2,q.y-.62,mod(q.z+1.5,3.)-1.5);
 hull=max(hull,-box(trench,vec3(.22,.12,1.25)));
 float deck=box(q-vec3(0,1.4,-1.8),vec3(4.7,.28,10.8));
 deck=max(deck,(abs(q.x)+max(q.z-3.,0.)*.38-4.7)*.9);
 hull=min(hull,deck);
 // Second deck tier and runway spine.
 hull=min(hull,max(box(q-vec3(1.2,1.82,-4.5),vec3(2.4,.16,6.5)),(abs(q.x-1.2)+max(q.z+1.,0.)*.5-2.4)*.9));
 // Turret rows on the deck edges: domes with twin barrels.
 vec3 t=vec3(abs(q.x)-4.1,q.y-1.82,q.z+1.);t.z-=4.*clamp(floor(t.z/4.+.5),-2.,1.);
 float turret=ellipsoid(t,vec3(.5,.32,.5));
 vec3 barrel=vec3(abs(t.x)-.13,t.y-.12,t.z-.55);
 turret=min(turret,box(barrel,vec3(.045,.045,.5)));
 hull=min(hull,turret);
 // Layered command tower, bridge band, mast and sensor dish.
 vec3 tower=q-vec3(-2.3,0,-5.);
 hull=min(hull,box(tower-vec3(0,2.5,0),vec3(1.3,.85,3.7))-.05);
 hull=min(hull,box(tower-vec3(0,3.55,-.5),vec3(1.05,.3,2.6))-.05);
 hull=min(hull,box(tower-vec3(0,4.15,-.8),vec3(.85,.32,1.6))-.05);
 hull=min(hull,max(box(tower-vec3(0,4.55,-.3),vec3(.7,.12,1.25)),(tower.z+tower.y*.6-3.2)));
 hull=min(hull,box(tower-vec3(0,5.6,-1.3),vec3(.06,1.15,.06)));
 hull=min(hull,box(tower-vec3(0,5.9,-1.3),vec3(.9,.045,.05)));
 hull=min(hull,box(tower-vec3(.5,5.2,-1.3),vec3(.04,.7,.04)));
 vec3 dish=tower-vec3(0,5.05,1.15);
 hull=min(hull,max(abs(length(dish)-.72)-.035,dish.y+.32));
 hull=min(hull,capsule(dish,vec3(0,-.75,0),vec3(0,-.1,0),.05));
 // Side hangar bays.
 vec3 bay=vec3(abs(q.x)-5.9,q.y-.05,q.z+1.5);
 hull=max(hull,-box(bay,vec3(1.5,.62,4.0)));
 // Engine housings: outboard pods with ribs and nozzle bells, inner pair below.
 float pod=max(length(vec2(abs(q.x)-5.3,q.y+.2))-1.2,abs(q.z+11.5)-4.0);
 float bell=max(abs(length(vec2(abs(q.x)-5.3,q.y+.2))-.95+(q.z+15.5)*.18)-.08,abs(q.z+15.9)-.45);
 pod=min(pod,bell);
 vec3 inner=vec3(abs(q.x)-2.1,q.y+.55,q.z+12.5);
 pod=smin(pod,max(length(inner.xy)-.75,abs(inner.z)-3.6),.25);
 pod=min(pod,max(abs(length(inner.xy)-.62+(inner.z+3.6)*.1)-.06,abs(inner.z+3.95)-.35));
 hull=min(hull,pod);
 // Large ribs around the pods establish readable industrial scale.
 float ribZ=mod(q.z+12.,2.5)-1.25;
 float ribs=max(length(vec2(abs(q.x)-5.3,q.y+.2))-1.3,abs(ribZ)-.12);
 ribs=max(ribs,abs(q.z+11.5)-3.7);hull=min(hull,ribs);
 return hull;
}
// Interceptor: blended fuselage with a needle nose and dorsal spine, side
// intakes, swept anhedral wings with wingtip cannons, canted twin fins, twin
// nacelles with recessed nozzles and a glass canopy. Materials: 1 hull,
// 2 engine light, 4 canopy.
vec2 fighter(vec3 q,float wing){
 float bound=length(q)-3.6;if(bound>.5)return vec2(bound,1.);
 float body=roundCone(q,vec3(0,0,-1.25),vec3(0,.03,1.1),.3,.14);
 body=smin(body,roundCone(q,vec3(0,.03,1.1),vec3(0,-.02,2.1),.14,.012),.08);
 body=smin(body,box(q-vec3(0,.17,-.45),vec3(.07,.08,.95))-.03,.07);
 body=max(body,-q.y-.27);
 vec3 iq=vec3(abs(q.x)-.3,q.y,q.z-.05);
 float intake=box(iq,vec3(.085,.12,.6))-.03;
 intake=max(intake,-box(iq-vec3(0,0,.6),vec3(.055,.085,.1)));
 body=smin(body,intake,.07);
 vec3 w=q-vec3(0,-.03,-.35);w.x=abs(w.x);w.y+=w.x*(.07-wing*.05);
 float span=1.05+wing*.6;
 float lead=w.z-(.78-w.x*.6),trail=-(w.z+.58-w.x*.16);
 float wings=max(max(lead,trail)*.8,max(w.x-span,abs(w.y)-.05*(1.-.55*w.x/span)));
 float gun=capsule(w,vec3(span-.04,0,-.55+span*.16),vec3(span-.04,0,.95-span*.45),.045);
 vec3 f=vec3(abs(q.x)-.22,q.y-.14,q.z+1.);f.xy=rot(-.38)*f.xy;
 float fins=max(box(f-vec3(0,.3,0),vec3(.022,.32,.36)),(f.z+f.y*.9-.36)*.7);
 vec3 e=vec3(abs(q.x)-.25,q.y+.03,q.z+1.08);
 float nacelle=max(length(e.xy)-.15,abs(e.z)-.5)-.02;
 nacelle=max(nacelle,-max(length(e.xy)-.1,abs(e.z+.55)-.12));
 float ship=min(min(min(body,wings),min(gun,fins)),nacelle);
 float canopy=ellipsoid(q-vec3(0,.21,.45),vec3(.13,.12,.42));
 float plume=.42+uPulse.x*.35;
 float along=clamp(-(e.z+.52)/(2.*plume),0.,1.);
 float engine=max(length(e.xy)-(.08+uPulse.x*.025)*(1.-.7*along),abs(e.z+.52+plume)-plume)*.9;
 if(engine<ship&&engine<canopy)return vec2(engine,2.);
 return canopy<ship?vec2(canopy,4.):vec2(ship,1.);
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
  float thrust=carrierExhaust(q,.7,1.1+uPulse.x);
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
  float thrust=carrierExhaust(q,.78,1.4+uPulse.x*1.3);
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
 float lens=1.35;
 cinematicRig(ro,target,p,lens,uVariant==2?.25:.85,.8);
 vec3 rd=cameraBasis(ro,target)*normalize(vec3(p,lens));
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
  // Hull occlusion and a cool rim light give the fleet mass against space.
  float ao=clamp(fleet(at+n*.25).x/.25*.6+fleet(at+n*.8).x/.8*.4,.2,1.);c*=ao;
  c+=vec3(.2,.45,.9)*pow(1.-max(dot(n,-rd),0.),4.)*.45;
  c+=neon*(edge*.16+line(abs(q.x)-.3,.015)*.3+line(q.z-.8,.02)*.3);
  if(h.y>3.5){
   // Tinted canopy glass: dark body, sharp sky reflection and an amber HUD glint.
   c=vec3(.01,.02,.035)+vec3(.35,.6,1.)*pow(1.-max(dot(n,-rd),0.),3.)*.8+vec3(.6,.8,1.)*spec*1.6;
   c+=vec3(1.,.55,.15)*.12*(.5+.5*sin(at.z*9.));
  }else if(h.y>2.5){
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
 c+=neon*glow*(.10+uPulse.x*.10);c+=speedDust(p,lens,vec3(.45,.65,1.));color=vec4(c,1.);
}
