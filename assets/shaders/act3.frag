#version 330 core
in vec2 vUV;
out vec4 color;
uniform float uTime,uEnergy;
uniform vec2 uResolution;
uniform int uScene,uRetro;
#include "common.glsl"

// Act III is a directed signed-distance performance. Occurrences are distinct
// set pieces, not a camera index modulo four. Chapter progress never wraps;
// uAction changes the geometry once, at the selected musical accent.
float solidBox(vec3 p,vec3 b){
 vec3 q=abs(p)-b;
 return length(max(q,0.))+min(max(q.x,max(q.y,q.z)),0.);
}
float capsule(vec3 p,vec3 a,vec3 b,float radius){
 vec3 ab=b-a;float f=clamp(dot(p-a,ab)/max(dot(ab,ab),.0001),0.,1.);
 return length(p-a-ab*f)-radius;
}
float octahedron(vec3 p,float radius){return (abs(p.x)+abs(p.y)+abs(p.z)-radius)*.57735027;}
float ringShape(vec3 p,float radius,float width){return length(vec2(length(p.xz)-radius,p.y))-width;}
float ellipsoid(vec3 p,vec3 radii){
 float k0=length(p/radii),k1=length(p/(radii*radii));
 return k0*(k0-1.)/max(k1,.00001);
}
vec2 nearer(vec2 a,float distanceValue,float material){return distanceValue<a.x?vec2(distanceValue,material):a;}
float smin(float a,float b,float k){float h=clamp(.5+.5*(b-a)/k,0.,1.);return mix(b,a,h)-k*h*(1.-h);}
// Tapered capsule between two points; conservative for gentle tapers.
float roundCone(vec3 p,vec3 a,vec3 b,float ra,float rb){
 vec3 ab=b-a;float f=clamp(dot(p-a,ab)/max(dot(ab,ab),.0001),0.,1.);
 return (length(p-a-ab*f)-mix(ra,rb,f))*.9;
}
// A 2D outline extruded into a plate of half thickness t.
float extrudePlate(float outline,float y,float t){vec2 w=vec2(outline,abs(y)-t);return min(max(w.x,w.y),0.)+length(max(w,0.));}
float ellipse2(vec2 p,vec2 r){return (length(p/r)-1.)*min(r.x,r.y);}
// Two-bone limb: the knee solves both segment lengths in the plane holding the
// hip, the foot and a preferred bend direction.
vec3 limbKnee(vec3 hip,vec3 foot,float upper,float lower,vec3 bend){
 vec3 d=foot-hip;float len=clamp(length(d),.001,upper+lower-.001);vec3 dir=d/max(length(d),.001);
 float along=(upper*upper-lower*lower+len*len)/(2.*len);
 vec3 side=normalize(bend-dir*dot(bend,dir));
 return hip+dir*along+side*sqrt(max(upper*upper-along*along,0.));
}
vec3 radialCell(vec3 p,float count,float radius,float phase){
 float sector=2.*PI/count;
 float angle=atan(p.z,abs(p.x)+abs(p.z)>.00001?p.x:.00001)-phase;
 float folded=mod(angle+sector*.5,sector)-sector*.5;
 return vec3(cos(folded)*length(p.xz)-radius,p.y,sin(folded)*length(p.xz));
}
float development(){return smoothstep(0.,.72,uChapterProgress);}
// Transients articulate local mechanisms and light; they never reset the camera
// or replace the irreversible chapter action. The final horizon keeps its calm.
float kickResponse(){return clamp(uPulse.x,0.,1.);}
float snapResponse(){return clamp(uPulse.y,0.,1.);}
float highResponse(){return clamp(uPulse.z,0.,1.);}
float responseScale(){return uScene==15&&uVariant>=6?.22:(uScene==13&&uVariant==3?.45:1.);}
float desertHeight(vec2 p){return -2.+.34*sin(p.x*.12+p.y*.035)+.23*sin(p.y*.17-p.x*.04);}
vec2 desertFloor(vec3 p){return vec2((p.y-desertHeight(p.xz))*.8,0.);}

// The dune colossus: an armoured quadruped walking machine. A rounded chassis
// with dorsal plates and a sloped command head, side vents that recoil with the
// kick, four inverse-kinematic legs (hip housings, pistons, knee joints, tapered
// shins with power lines, clawed foot pads) in a four-beat walk whose stance feet
// travel with the ground. Material 1.4 is smooth machined armour, 2 dark
// mechanism, 3 light. Faces -z, the direction of travel.
vec2 walker(vec3 p,vec3 center,float size,float stride){
 vec3 q=(p-center)/size;
 float bound=length((q-vec3(0,2.3,0))*vec3(.85,1.,.85))-3.6;
 if(bound>.6)return vec2(bound*size*.8,1.4);
 q.y-=.06*sin(stride*2.+.6);
 vec3 b=q-vec3(0,2.85,0);
 float hull=solidBox(b,vec3(1.7,.42,.95))-.32;
 hull=smin(hull,ellipsoid(b+vec3(0,.42,0),vec3(1.85,.5,1.05)),.35);
 hull=max(hull,(abs(b.z)*.8+b.y-1.12)*.78);
 vec2 h=vec2(hull,1.4);
 vec3 plate=b-vec3(0,.66,0);plate.x=mod(plate.x+.33,.66)-.33;
 h=nearer(h,max(solidBox(plate,vec3(.25,.09,.62))-.04,abs(b.x)-1.62),1.4);
 h=nearer(h,solidBox(b-vec3(0,.6,0),vec3(1.72,.06,.2)),2.);
 // Command head: a sloped cab with a light slit and twin chin cannons.
 vec3 head=q-vec3(0,3.0,-1.38);
 float cab=solidBox(head,vec3(.62,.36,.52))-.12;
 cab=max(cab,(head.y-head.z*.6-.36)*.8);
 h.x=smin(h.x,cab,.2);
 h=nearer(h,solidBox(head-vec3(0,.08,-.62),vec3(.44,.05,.05)),3.);
 vec3 gun=vec3(abs(head.x)-.36,head.y+.28,head.z);
 h=nearer(h,capsule(gun,vec3(0,0,-.3),vec3(0,-.05,-1.25),.075),2.);
 // Side cooling grilles recoil outward on the kick.
 vec3 vent=vec3(abs(b.x)-1.98-kickResponse()*.07,b.y-.02,b.z);
 vent.z=mod(vent.z+.2,.4)-.2;
 h=nearer(h,max(solidBox(vent,vec3(.05,.2,.11)),abs(b.z)-.7),3.);
 // Sensor mast at the rear.
 h=nearer(h,capsule(q,vec3(.4,3.4,1.),vec3(.55,4.6,1.6),.045),2.);
 h=nearer(h,length(q-vec3(.55,4.65,1.6))-.09,3.);
 for(int i=0;i<4;++i){
  float side=i<2?-1.:1.,front=(i==0||i==2)?-1.:1.;
  // Four-beat walk: left hind, left fore, right hind, right fore.
  float offset=i==1?0.:(i==0?.25:(i==3?.5:.75));
  float phase=stride+offset*2.*PI;
  float lift=.5*pow(max(sin(phase),0.),1.3);
  vec3 hip=vec3(side*1.6,2.5,front*.92);
  vec3 foot=vec3(side*2.4,.06+lift,front*1.02+.3*cos(phase));
  vec3 ankle=foot+vec3(0,.34,0);
  vec3 knee=limbKnee(hip,ankle,1.55,1.75,vec3(side,.95,front*.3));
  h=nearer(h,length(q-hip)-.42,2.);
  h=nearer(h,solidBox(q-hip-vec3(side*.12,.38,0),vec3(.48,.12,.5))-.08,1.4);
  h=nearer(h,roundCone(q,hip,knee,.3,.22),1.4);
  h=nearer(h,capsule(q,hip+vec3(0,.36,0),mix(hip,knee,.78)+vec3(0,.24,0),.065),2.);
  h=nearer(h,length(q-knee)-.27,2.);
  h=nearer(h,roundCone(q,knee,ankle,.24,.14),1.4);
  h=nearer(h,capsule(q,knee+vec3(side*.2,0,0),ankle+vec3(side*.15,.1,0),.035),3.);
  h=nearer(h,length(q-ankle)-.17,2.);
  vec3 f=q-foot;
  float pad=max(length(f.xz*vec2(1.,.85))-.4,abs(f.y)-.1)-.04;
  vec3 toe=vec3(abs(f.x),f.y,f.z);
  pad=min(pad,capsule(toe,vec3(.24,-.02,-.28),vec3(.3,-.08,-.6),.075));
  pad=min(pad,capsule(f,vec3(0,-.02,-.3),vec3(0,-.08,-.68),.08));
  h=nearer(h,pad,2.);
 }
 h.x*=size;return h;
}

vec2 desertWorld(vec3 p){
 vec2 h=desertFloor(p);float progress=uChapterProgress,action=uAction;
 if(uVariant==0){
  // First reveal: a buried sun gate rises; its two halves unlock once.
  float rise=smoothstep(.04,.7,progress),split=action*1.8;
  vec3 q=p-vec3(0,-8.+rise*12.,0);
  q.x=abs(q.x)-3.6-split;
  h=nearer(h,solidBox(q,vec3(1.35,5.3,.85))-.16,1.);
  h=nearer(h,solidBox(q-vec3(-1.38,0,.88),vec3(.045,4.8,.055)),3.);
  h=nearer(h,solidBox(p-vec3(0,-2.8+rise*11.,0),vec3(5.4+split,.65,.95))-.12,1.);
  vec3 sun=p-vec3(0,5.5,-13.);sun.yz=rot(PI*.5)*sun.yz;
  h=nearer(h,ringShape(sun,3.1,.12),3.);
 }else if(uVariant==1){
  // One low tracking shot beside an actual articulated giant, then a leap.
  float leap=sin(PI*clamp((progress-.55)/.4,0.,1.))*action*2.5;
  vec3 center=vec3(0,-1.8+leap,-progress*7.);
  vec2 giant=walker(p,center,1.3,uChapterTime*2.7);
  if(giant.x<h.x)h=giant;
  vec3 monolith=p;monolith.z=mod(monolith.z+10.,20.)-10.;
  monolith.x=abs(monolith.x)-15.;
  h=nearer(h,solidBox(monolith-vec3(0,2.,0),vec3(1.1,4.,1.4))-.08,2.);
 }else if(uVariant==2){
  // A canyon chase. The passage opens into a storm after the accent.
  float bend=sin(p.z*.075)*2.2;
  vec3 wall=p;wall.x=abs(p.x-bend)-(7.+action*4.);
  wall.z=mod(wall.z+4.,8.)-4.;
  float top=6.+2.*sin(floor((p.z+4.)/8.)*2.3);
  h=nearer(h,solidBox(wall-vec3(0,top*.5-2.,0),vec3(2.4,top*.5,3.3))-.18,1.);
  vec3 rib=p;rib.x-=bend;rib.z=mod(rib.z+7.,14.)-7.;
  float arch=abs(length(vec2(rib.x,(rib.y+2.)*.8))-6.7)-.12;
  h=nearer(h,max(arch,abs(rib.z)-.2),2.);
  vec3 spark=p;spark.x-=bend;spark.z=mod(spark.z+3.,6.)-3.;
  h=nearer(h,solidBox(vec3(abs(spark.x)-4.6,spark.y+1.55,spark.z),vec3(.045,.06,1.2)),3.);
 }else if(uVariant==3){
  // The titan becomes a six-arm machine: folded sections deploy radially.
  vec3 q=p-vec3(0,3.4,0);
  h=nearer(h,solidBox(q,vec3(1.35,3.5,1.35))-.15,1.);
  vec3 arm=radialCell(q,6.,0.,.25+progress*.45);
  arm.xy=rot(mix(-1.15,.18,action)+kickResponse()*.09)*arm.xy;
  h=nearer(h,solidBox(arm-vec3(4.,1.5,0),vec3(3.2,.36,.62))-.1,1.);
  h=nearer(h,octahedron(arm-vec3(7.4,1.5,0),1.15),2.);
  h=nearer(h,solidBox(arm-vec3(4.,1.9,0),vec3(2.7,.035,.19)),3.);
  h=nearer(h,capsule(arm,vec3(1.3,1.05,0),vec3(5.6+snapResponse()*.35,1.05,0),.11),2.);
  h=nearer(h,ringShape(q-vec3(0,-2.,0),3.8+action*3.,.11),3.);
 }else if(uVariant==4){
  // A levitating solar train overtakes the camera on an elevated causeway.
  vec3 track=p;track.x=abs(track.x)-1.65;
  h=nearer(h,solidBox(track-vec3(0,1.,0),vec3(.12,.12,100.)),3.);
  vec3 pier=p;pier.z=mod(p.z+6.,12.)-6.;
  h=nearer(h,solidBox(pier-vec3(0,-.4,0),vec3(2.8,1.4,.65)),1.);
  vec3 car=p;car.z=mod(p.z+uChapterTime*9.+9.,18.)-9.;
  car.y-=2.7+action*(.8+.15*sin(uChapterTime*1.5));
  h=nearer(h,solidBox(car,vec3(2.5,.8,5.8))-.3,1.);
  h=nearer(h,solidBox(car-vec3(0,.87,0),vec3(1.65,.07,4.4)),2.);
  h=nearer(h,solidBox(vec3(abs(car.x)-2.63,car.y,car.z),vec3(.035,.15,4.9)),3.);
  vec3 window=car;window.z=mod(window.z+.7,1.4)-.7;
  h=nearer(h,max(solidBox(vec3(abs(window.x)-2.77,window.y-.28,window.z),vec3(.02,.22,.38)),abs(car.z)-4.8),2.);
 }else{
  // Final desert visit: the floor opens and an entire launch field departs.
  vec2 cell=clamp(floor((p.xz+4.)/8.),vec2(-2.),vec2(2.));
  vec3 q=p;q.xz=p.xz-cell*8.;
  float seed=hash(cell),launch=smoothstep(seed*.3,.9,action);
  float lift=1.+launch*launch*32.;
  if(max(abs(cell.x),abs(cell.y))<2.5){
   h=nearer(h,solidBox(q-vec3(0,-1.1,0),vec3(3.4,.55,3.4))-.08,2.);
   // A slender launcher: tapered hull and needle nose, three swept fins,
   // a nozzle bell, a light band and the exhaust column once it lifts.
   vec3 ship=(q-vec3(0,lift+2.6,0))/1.35;
   float hull=roundCone(ship,vec3(0,-2.4,0),vec3(0,2.,0),.62,.22);
   hull=smin(hull,roundCone(ship,vec3(0,2.,0),vec3(0,3.5,0),.22,.015),.12);
   h=nearer(h,hull*1.35,1.4);
   vec3 fin=radialCell(ship,3.,0.,seed*6.);
   float fins=max(solidBox(fin-vec3(.72,-1.85,0),vec3(.5,.78,.035)),(fin.x+fin.y*.8+.55)*.7);
   h=nearer(h,fins*1.35,2.);
   h=nearer(h,roundCone(ship,vec3(0,-2.95,0),vec3(0,-2.4,0),.5,.36)*1.35,2.);
   h=nearer(h,(length(vec2(length(ship.xz)-.33,ship.y-.9))-.045)*1.35,3.);
   h=nearer(h,solidBox(ship+vec3(0,3.+.8+launch*2.5,0),vec3(.1,.8+launch*2.5,.1))*1.35,3.);
  }
 }
 return h;
}

vec2 gardenFloor(vec3 p){return vec2((p.y+1.85+.08*sin(p.x*.4)*sin(p.z*.3))*.9,0.);}
vec2 sprout(vec3 p,float growth,float petals){
 vec2 h=vec2(180.,0.);float height=.2+growth*7.;
 h=nearer(h,capsule(p,vec3(0,-1.8,0),vec3(.3*sin(growth*2.),height,0),.22),1.);
 for(int level=0;level<3;++level){
  vec3 branch=radialCell(p,petals,0.,float(level)*.55);
  float y=(.4+float(level)*1.8)*growth,reach=(3.8-float(level)*.65)*growth;
  vec3 tip=vec3(reach,y+growth*1.5,0);
  h=nearer(h,capsule(branch,vec3(0,y,0),tip,.1),1.);
  vec3 leaf=branch-tip;leaf.xy=rot(.5-uAction*.35-kickResponse()*.13+highResponse()*.06)*leaf.xy;
  h=nearer(h,ellipsoid(leaf,vec3(.12+growth,.12+.22*growth,.1+.52*growth)),2.);
  h=nearer(h,length(branch-tip-vec3(.2,.4,0))-(.025+.06*growth),3.);
 }
 return h;
}
vec2 gardenWorld(vec3 p){
 vec2 h=gardenFloor(p);float action=uAction,progress=uChapterProgress;
 if(uVariant==0){
  // A seed germinates into a branching living circuit, visibly once.
  vec2 plant=sprout(p,development(),5.);
  if(plant.x<h.x)h=plant;
  vec3 root=radialCell(p,7.,0.,0.);
  h=nearer(h,capsule(root,vec3(0,-1.7,0),vec3(5.*action,-1.65,0),.07),3.);
 }else if(uVariant==1){
  // Walk up a gigantic double helix; the ladder unzips on the accent.
  vec3 q=p;q.xz=rot(q.y*.47+uChapterTime*.23)*q.xz;
  float spread=2.6+action*1.9;
  vec3 helix=q;helix.x=abs(helix.x)-spread;
  float poles=length(helix.xz)-.24;
  poles=max(poles,max(-q.y-1.7,q.y-14.));
  h=nearer(h,poles*.4,1.);
  vec3 rung=q;rung.y=mod(q.y+.7,1.4)-.7;
  float ladder=solidBox(rung,vec3(spread,.07,.12));
  h=nearer(h,max(ladder,max(-q.y-1.5,q.y-13.5))*.4,3.);
  vec3 leaf=q;leaf.y=mod(q.y+1.4,2.8)-1.4;leaf.x=abs(leaf.x)-spread-1.;
  h=nearer(h,max(ellipsoid(leaf,vec3(1.1,.55,.3)),max(-q.y,q.y-13.))*.4,2.);
 }else if(uVariant==2){
  // Macro flight through overlapping six-petal seed gates, then a burst open.
  vec3 q=p;q.z=mod(p.z+4.,8.)-4.;
  q.xy=rot(floor((p.z+4.)/8.)*.55)*q.xy;
  vec3 petal=radialCell(vec3(q.x,q.z,q.y),6.,3.8+action*2.7,0.);
  petal.xy=rot(.35)*petal.xy;
  h=nearer(h,ellipsoid(petal,vec3(1.7,.28,.75)),2.);
  h=nearer(h,ringShape(vec3(q.x,q.z,q.y),3.1+action*2.7,.055),3.);
  // The flying seed has a long, readable luminous tail in the open passage.
  vec3 mote=p-vec3(sin(p.z*.17)*1.3,cos(p.z*.13)*.7,0.);
  mote.z=mod(p.z+uChapterTime*12.+6.,12.)-6.;
  h=nearer(h,ellipsoid(mote,vec3(.08,.08,.65)),3.);
 }else if(uVariant==3){
  // A breathing pause: one floating lotus opens over a quiet pool.
  vec3 q=radialCell(p-vec3(0,.1,0),8.,0.,.2);
  q.xy=rot(mix(-.9,.16,action)-kickResponse()*.045)*q.xy;
  h=nearer(h,ellipsoid(q-vec3(2.65,0,0),vec3(2.5,.28,1.)),2.);
  vec3 center=p-vec3(0,.3,0);
  h=nearer(h,ellipsoid(center,vec3(1.1,.65,1.1)),1.);
  h=nearer(h,ringShape(p-vec3(0,-1.65,0),2.2+progress*5.,.025),3.);
 }else if(uVariant==4){
  // Terraced leaves unfold into an illuminated cascade across the landscape.
  vec2 cell=clamp(floor((p.xz+2.5)/5.),vec2(-3.),vec2(3.));
  vec3 q=p;q.xz=p.xz-cell*5.;
  float seed=hash(cell),growth=smoothstep(seed*.4,.85,progress);
  float height=1.+growth*(3.+cell.y*.42+seed*3.);
  if(max(abs(cell.x),abs(cell.y))<3.5){
   h=nearer(h,solidBox(q-vec3(0,height*.5-1.8,0),vec3(.18,height*.5,.18)),1.);
   vec3 leaf=q-vec3(0,height-1.6,0);leaf.xy=rot((1.-action)*.95+kickResponse()*.1*sin(seed*6.28))*leaf.xy;
   h=nearer(h,ellipsoid(leaf,vec3(2.25,.17,2.)),2.);
   h=nearer(h,solidBox(q-vec3(2.,height*.5-1.5,1.8),vec3(.035,height*.5,.035)),3.);
  }
 }else{
  // The whole frame blooms: a huge flower unfolds from a narrow bud.
  vec3 q=p-vec3(0,3.4,0);
  h=nearer(h,capsule(p,vec3(0,-1.8,0),vec3(0,3.4,0),.38),1.);
  for(int layer=0;layer<2;++layer){
   float l=float(layer);vec3 petal=radialCell(q,10.+l*3.,0.,l*.21);
   petal.xy=rot(mix(-1.35,.18+l*.18,action)-kickResponse()*.12+highResponse()*.035*l)*petal.xy;
   petal-=vec3(4.4-l,0.,0.);
   h=nearer(h,ellipsoid(petal,vec3(4.3-l,.28,.85)),2.);
   h=nearer(h,capsule(petal,vec3(-3.8+l,0,0),vec3(3.8-l,0,0),.035),3.);
  }
  h=nearer(h,octahedron(q,1.8+.4*kickResponse()),3.);
 }
 return h;
}

vec2 gravityWorld(vec3 p){
 vec2 h=vec2(180.,0.);float action=uAction,progress=uChapterProgress;
 if(uVariant==0){
  // A recognizable floor breaks into rising islands, not another orbiting core.
  vec2 base=clamp(floor((p.xz+3.)/6.),vec2(-2.),vec2(2.));
  for(int ix=-1;ix<=1;++ix)for(int iz=-1;iz<=1;++iz){
   vec2 cell=clamp(base+vec2(float(ix),float(iz)),vec2(-2.),vec2(2.));
   vec3 q=p;q.xz=p.xz-cell*6.;
   float seed=hash(cell+8.),lift=action*(2.+seed*10.);
   q.y-=lift-1.8;
   h=nearer(h,solidBox(q,vec3(2.7,.6,2.7))-.08,1.);
   h=nearer(h,solidBox(q-vec3(0,.65,0),vec3(2.45,.025,2.45)),2.);
   h=nearer(h,solidBox(vec3(abs(q.x)-2.72,q.y,q.z),vec3(.025,.3,2.3)),3.);
  }
 }else if(uVariant==1){
  // Falling horizons: one long descending staircase shears away behind us.
  float base=clamp(floor((p.z+4.)/8.),-10.,10.);
  for(int i=-1;i<=1;++i){
   float cell=clamp(base+float(i),-10.,10.);vec3 q=p;
   q.z=p.z-cell*8.;q.x-=sin(cell*1.7)*4.;
   q.y-=cell*3.-action*hash(vec2(cell,4.))*5.;
   h=nearer(h,solidBox(q,vec3(4.3,.55,3.1))-.12,1.);
   h=nearer(h,solidBox(q-vec3(0,.58,2.8),vec3(4.,.035,.09)),3.);
   h=nearer(h,solidBox(vec3(abs(q.x)-4.15,q.y+2.1,q.z),vec3(.1,2.1,.15)),2.);
  }
 }else if(uVariant==2){
  // A continental plate tears in half, exposing a bright fault line.
  for(int i=0;i<2;++i){
   float side=i==0?-1.:1.;vec3 q=p-vec3(side*action*6.,side*action*1.7,0);
   q.yz=rot(side*action*.22)*q.yz;
   float slab=max(solidBox(q,vec3(8.,1.25,7.)),-side*q.x+.12);
   slab=max(slab,(abs(q.x)+abs(q.z)-12.5)*.7071);
   h=nearer(h,slab,1.);
   h=nearer(h,solidBox(q-vec3(side*.2,0,0),vec3(.09,1.26,6.2)),3.);
  }
  vec3 debris=radialCell(p,11.,4.+action*7.,progress*.45);
  debris.y-=2.+action*3.;debris.xy=rot(progress)*debris.xy;
  h=nearer(h,octahedron(debris,.7),2.);
 }else if(uVariant==3){
  // An orbital uprising has vertical silhouettes and rods shooting skyward.
  vec3 q=radialCell(p-vec3(0,2.,0),9.,8.,.15);
  float launch=action*action*13.;
  q.y-=launch;
  h=nearer(h,solidBox(q,vec3(.8,4.8,.8))-.12,1.);
  h=nearer(h,solidBox(q-vec3(0,-6.,0),vec3(.08,1.3+action*2.,.08)),3.);
  vec3 ring=p-vec3(0,2.+launch*.35,0);ring.yz=rot(PI*.5)*ring.yz;
  h=nearer(h,ringShape(ring,7.,.12),2.);
  h=nearer(h,ringShape(p-vec3(0,-3.,0),12.,.05),3.);
 }else{
  // Reality behaves like a book: four massive walls fold open at the accent.
  for(int i=0;i<2;++i){
   float side=i==0?-1.:1.;vec3 q=p-vec3(side*6.,1.,-3.);
   q.xz=rot(side*action*1.5)*q.xz;
   h=nearer(h,solidBox(q-vec3(0,0,-5.),vec3(.35,6.,5.))-.08,1.);
   h=nearer(h,solidBox(q-vec3(-side*.38,0,-5.),vec3(.04,5.7,4.5)),3.);
  }
  h=nearer(h,solidBox(p-vec3(0,-4.5,-3.),vec3(6.,.2,16.)),2.);
  vec3 portal=p-vec3(0,2.,-23.);portal.yz=rot(PI*.5)*portal.yz;
  h=nearer(h,ringShape(portal,5.5+action*1.7,.18),3.);
 }
 return h;
}

vec2 finaleWorld(vec3 p){
 float action=uAction,progress=uChapterProgress;
 vec2 h=vec2(p.y+2.,0.);
 if(uVariant==0){
  // An archway assembles ahead of a forward-moving causeway camera.
  vec3 q=p;q.z=mod(p.z+6.,12.)-6.;
  float grow=smoothstep(.02,.65,progress);
  float radius=length(vec2(q.x*.75,q.y+2.));
  float arc=max(abs(radius-7.)-.24,abs(q.z)-.45);
  arc=max(arc,-q.y-2.+(1.-grow)*15.);
  h=nearer(h,arc,1.);
  vec3 seam=q;seam.z-=.48;
  h=nearer(h,max(abs(length(vec2(seam.x*.75,seam.y+2.))-7.)-.065,abs(seam.z)-.04),3.);
  h=nearer(h,solidBox(p-vec3(0,-1.5,0),vec3(2.7,.35,100.)),2.);
 }else if(uVariant==1){
  // A crystal moth spreads enormous wings: a head with compound eyes, a
  // furred thorax and segmented abdomen, six folded legs, glowing feathered
  // antennae, and moth-shaped forewings and swallowtail hindwings with light
  // veins, rims and eyespots. Wings hinge open at the musical action.
  vec3 q=p-vec3(0,3.,0);q.yz=rot(.08*sin(uChapterTime*.8))*q.yz;
  float bound=length(q*vec3(.62,1.2,.8))-5.2;
  if(bound>1.){h=nearer(h,bound*.62,1.4);}
  else{
   float body=length(q-vec3(0,.05,-2.45))-.48;
   body=smin(body,ellipsoid(q-vec3(0,.12,-1.25),vec3(.82,.74,1.05)),.25);
   for(int i=0;i<6;++i){float f=float(i);
    body=smin(body,ellipsoid(q-vec3(0,.02-f*.05,.2+f*.56),vec3(.7-f*.085,.6-f*.07,.4)),.1);}
   h=nearer(h,body,1.4);
   vec3 a=vec3(abs(q.x),q.y,q.z);
   h=nearer(h,length(a-vec3(.36,.16,-2.72))-.27,2.);
   vec3 s0=vec3(.18,.42,-2.8),s1=vec3(1.3,1.5,-4.35),ab=s1-s0;
   float t=clamp(dot(a-s0,ab)/dot(ab,ab),0.,1.);vec3 axisPoint=s0+ab*t;
   float radial=length(a-axisPoint);
   float comb=max(abs(mod(t*length(ab),.13)-.065)-.013,radial-.3*(1.-t*.75));
   comb=max(comb,max(dot(a-s1,normalize(ab)),-dot(a-s0,normalize(ab))));
   h=nearer(h,min(radial-.045,comb*.8),3.);
   for(int i=0;i<3;++i){float f=float(i);
    vec3 hip=vec3(.42,-.35,-1.65+f*.5),knee=vec3(1.2,-.2+f*.05,-1.95+f*.8),foot=vec3(1.5,-1.,-2.25+f*1.1);
    h=nearer(h,min(capsule(a,hip,knee,.075),capsule(a,knee,foot,.05)),1.4);
   }
   vec3 w=q;w.x=abs(w.x)-.55;
   float hinge=mix(-1.15,.08,action)+.05*sin(uChapterTime*2.1)+kickResponse()*.05;
   w.xy=rot(hinge)*w.xy;
   float wy=w.y-.012*w.x*w.x;
   vec2 u=w.xz;
   float fore=ellipse2(rot(.38)*(u-vec2(2.7,-.95)),vec2(2.95,1.1));
   fore=smin(fore,ellipse2(u-vec2(4.9,-2.),vec2(1.15,.8)),.5);
   fore=smin(fore,ellipse2(u-vec2(.9,-.55),vec2(1.,.9)),.4);
   float hind=ellipse2(rot(-.5)*(u-vec2(2.1,1.25)),vec2(2.2,1.4));
   hind=smin(hind,ellipse2(u-vec2(2.9,2.85),vec2(.55,1.05)),.45);
   float wings=min(extrudePlate(fore,wy-.06,.035),extrudePlate(hind,wy+.06,.035));
   h=nearer(h,wings*.85,2.4);
   // Light inlaid in each membrane: veins fanning from the hinge, a rim inside
   // each edge and an eyespot per wing.
   float veins=100.;
   for(int i=0;i<5;++i){float an=-.95+float(i)*.42;vec2 dir=vec2(cos(an),sin(an));
    float along=clamp(dot(u,dir),0.,6.);veins=min(veins,length(u-dir*along)-.028);}
   float foreLight=max(min(min(veins,abs(fore+.09)-.03),abs(length(u-vec2(3.15,-1.05))-.5)-.05),fore);
   float hindLight=max(min(min(veins,abs(hind+.09)-.03),length(u-vec2(2.3,1.45))-.22),hind);
   h=nearer(h,min(extrudePlate(foreLight,wy-.06,.05),extrudePlate(hindLight,wy+.06,.05))*.85,3.);
  }
 }else if(uVariant==2){
  // Three separate portals converge into one triangular impossible machine.
  for(int i=0;i<3;++i){
   float a=float(i)*2.*PI/3.+PI*.5;
   vec3 center=vec3(cos(a)*7.,4.+sin(a)*5.,0.)*(1.-action*.55);
   center.y+=action*2.;vec3 q=p-center;
   q.xy=rot(a+progress*.6)*q.xy;q.yz=rot(PI*.5+action*.4)*q.yz;
   h=nearer(h,ringShape(q,3.2,.23),1.);
   h=nearer(h,ringShape(q,2.85,.055),3.);
  }
  vec3 heart=p-vec3(0,4.,0);heart.xz=rot(uChapterTime*.4)*heart.xz;
  h=nearer(h,octahedron(heart,.15+action*2.8),2.);
 }else if(uVariant==3){
  // A flat array stands up into a crystalline city, row after row.
  vec2 base=clamp(floor((p.xz+3.)/6.),vec2(-3.),vec2(3.));
  for(int ix=-1;ix<=1;++ix)for(int iz=-1;iz<=1;++iz){
   vec2 cell=clamp(base+vec2(float(ix),float(iz)),vec2(-3.),vec2(3.));
   vec3 q=p;q.xz=p.xz-cell*6.;
   float seed=hash(cell+14.),growth=smoothstep(seed*.38,.88,progress);
   float height=.2+growth*(4.+seed*11.);
   q.y+=2.-height*.5;q.xy=rot((1.-action)*.45*sin(cell.y))*q.xy;
   h=nearer(h,solidBox(q,vec3(1.3,height*.5,1.3))-.13,1.);
   h=nearer(h,solidBox(q-vec3(1.45,0,0),vec3(.035,height*.44,1.)),3.);
   h=nearer(h,octahedron(q-vec3(0,height*.5,0),1.9),2.);
  }
 }else if(uVariant==4){
  // Penultimate landscape: three unlike monuments and a bridge unite the acts.
  vec3 gate=p-vec3(0,3.1,0);
  float g=development();
  h=nearer(h,solidBox(vec3(abs(gate.x)-3.4,gate.y,gate.z),vec3(.65,5.1*g,.9))-.1,1.);
  h=nearer(h,solidBox(gate-vec3(0,5.2*g,0),vec3(4.,.55,.9))-.1,1.);
  vec3 tree=p-vec3(-11.,0.,-1.);tree.xz=rot(tree.y*.35)*tree.xz;
  h=nearer(h,capsule(tree,vec3(0,-1.8,0),vec3(0,7.*g,0),.3),2.);
  for(int level=0;level<3;++level){
   float l=float(level);vec3 branch=radialCell(tree,5.,0.,l*.6);
   vec3 tip=vec3((3.3-l*.55)*g,1.5+(1.4+l*1.5)*g,0.);
   h=nearer(h,capsule(branch,vec3(0,tip.y-1.3,0),tip,.1)*.65,2.);
   h=nearer(h,ellipsoid(branch-tip,vec3(1.6,.28,.7))*.65,2.);
  }
  vec3 crystal=p-vec3(11.,3.*g,-1.);crystal.xz=rot(progress*.6)*crystal.xz;
  h=nearer(h,octahedron(crystal/vec3(1.1,2.,1.1),3.*g+.1)*1.1,2.);
  vec3 crystalCage=radialCell(p-vec3(11.,3.*g,-1.),7.,3.+g,progress*.4);
  h=nearer(h,octahedron(crystalCage/vec3(.4,1.6,.4),.9)*.4,2.);
  h=nearer(h,capsule(p,vec3(-11.,7.,-1.),vec3(-11.+action*22.,7.,-1.),.09),3.);
  h=nearer(h,solidBox(p-vec3(0,-1.6,0),vec3(17.,.25,5.)),1.);
  h=nearer(h,solidBox(p-vec3(0,-1.85,0),vec3(19.,.2,6.3)),2.);
  // A real monumental setting: two tiers, a roofline and tall rear colonnades.
  float column=clamp(floor((p.x+3.)/6.),-3.,3.);
  vec3 col=p-vec3(column*6.,3.5,-9.);
  h=nearer(h,solidBox(col,vec3(.45,5.5,.65))-.08,1.);
  h=nearer(h,solidBox(col-vec3(0,0,.72),vec3(.055,4.9,.025)),3.);
  h=nearer(h,solidBox(p-vec3(0,9.,-9.),vec3(20.,.6,1.1))-.1,1.);
  vec3 crown=p-vec3(0,4.,-10.);
  float crownArc=max(abs(length(vec2(crown.x*.65,crown.y))-13.)-.22,abs(crown.z)-.35);
  h=nearer(h,max(crownArc,-crown.y),2.);
 }else if(uVariant==5){
  // The final ascent is a first-person climb through a vertical cathedral.
  h=vec2(180.,0.);
  vec3 q=p;q.y=mod(p.y+3.5,7.)-3.5;
  h=nearer(h,ringShape(vec3(p.x,q.y,p.z),7.8,.3),1.);
  h=nearer(h,ringShape(vec3(p.x,q.y-.35,p.z),7.5,.045),3.);
  vec3 rib=radialCell(p,12.,7.8,0.);
  h=nearer(h,solidBox(rib,vec3(.15,180.,.15)),2.);
  vec3 panel=radialCell(vec3(p.x,q.y,p.z),12.,7.6,0.);
  h=nearer(h,solidBox(panel,vec3(.12,2.6,1.35)),1.);
  h=nearer(h,solidBox(panel-vec3(-.13,0,0),vec3(.025,2.2,.04)),3.);
  vec3 ceiling=p-vec3(0,145.,0);
  h=nearer(h,octahedron(ceiling,8.+action*5.),3.);
 }else{
  // Ending: a star breaks once, debris leaves the frame, a clean sun remains.
  h=vec2((p.y+2.+.055*sin(p.x*.9+p.z*.4-uTime*.5))*.95,0.);
  vec3 core=p-vec3(0,2.2,0);core.xy=rot(progress*.6)*core.xy;
  if(action<.995)h=nearer(h,octahedron(core,3.4*(1.-action)),2.);
  if(action>.001){
   vec3 shard=radialCell(p-vec3(0,2.2+action*8.,0),12.,3.+action*45.,progress*.8);
   shard.xy=rot(progress*2.)*shard.xy;
   h=nearer(h,octahedron(shard/vec3(.5,1.5,.5),1.5)*.5,2.);
  }
  vec3 sun=p-vec3(0,4.5+progress*4.,-32.);
  h=nearer(h,length(sun)-4.6,3.);
 }
 return h;
}

vec2 sceneField(vec3 p){
 vec2 h;
 if(uScene==12){
  h=desertWorld(p);
  // Distant ruins anchor the hero to a landscape and establish human scale.
  vec3 ruin=p;ruin.x=abs(p.x)-28.;ruin.z=mod(p.z+12.,24.)-12.;
  h=nearer(h,solidBox(ruin-vec3(0,5.,0),vec3(2.,7.5,3.2))-.18,2.);
  h=nearer(h,solidBox(ruin-vec3(0,12.7,0),vec3(2.5,.5,10.5))-.12,1.);
  h=nearer(h,solidBox(ruin-vec3(-2.05,5.,0),vec3(.035,5.7,.18)),3.);
 }else if(uScene==13){
  h=gardenWorld(p);
  // A peripheral forest supports the main growth without covering its silhouette.
  vec3 tree=radialCell(p,16.,24.,.1);
  if(uVariant==2){tree=p;tree.x=abs(p.x)-12.;tree.z=mod(p.z+8.,16.)-8.;}
  h=nearer(h,capsule(tree,vec3(0,-1.8,0),vec3(.6,12.,0),.34),1.);
  vec3 crown=radialCell(tree-vec3(.6,9.8,0),5.,2.1,.2);
  h=nearer(h,ellipsoid(crown,vec3(2.8,.22,.9)),2.);
  h=nearer(h,capsule(tree,vec3(.4,7.,0),vec3(3.8,10.,0),.12),1.);
 }else if(uScene==14){
  h=gravityWorld(p);
 }else{
  h=finaleWorld(p);
  if(uVariant<5){
   vec3 buttress=p;buttress.x=abs(p.x)-34.;buttress.z=mod(p.z+8.,16.)-8.;
   h=nearer(h,solidBox(buttress-vec3(0,6.,0),vec3(1.1,8.,2.2))-.12,1.);
   h=nearer(h,solidBox(buttress-vec3(0,14.,0),vec3(2.,.35,6.8)),2.);
   h=nearer(h,solidBox(buttress-vec3(-1.15,6.,0),vec3(.025,6.2,.12)),3.);
  }
 }
 return h;
}
float cellCrossing(float position,float direction,float spacing){
 if(abs(direction)<.00001)return 10000.;
 float cell=floor((position+spacing*.5)/spacing);
 float edge=(cell+(direction>0.?1.:0.))*spacing-spacing*.5;
 return max((edge-position)/direction,.0001)+.001;
}
float sceneBoundaryStep(vec3 p,vec3 direction){
 // Unequal cell heights are not a globally conservative distance field.
 // Evaluate neighbors above and never march past a change of cell ownership.
 if(uScene==14&&uVariant==1)return cellCrossing(p.z,direction.z,8.);
 if(uScene==12&&uVariant==2)return cellCrossing(p.z,direction.z,8.);
 float spacing=0.;
 if(uScene==12&&uVariant==5)spacing=8.;
 if(uScene==13&&uVariant==4)spacing=5.;
 if(uScene==14&&uVariant==0||uScene==15&&uVariant==3)spacing=6.;
 if(spacing==0.)return 10000.;
 return min(cellCrossing(p.x,direction.x,spacing),cellCrossing(p.z,direction.z,spacing));
}
vec3 fieldNormal(vec3 p){
 const vec2 e=vec2(.004,-.004);
 vec3 n=e.xyy*sceneField(p+e.xyy).x+e.yyx*sceneField(p+e.yyx).x+
        e.yxy*sceneField(p+e.yxy).x+e.xxx*sceneField(p+e.xxx).x;
 return n/max(length(n),.00001);
}
vec3 atmosphere(vec3 ray){
 float horizon=clamp(ray.y*.5+.5,0.,1.);
 vec2 sky=ray.xz/max(abs(ray.y)+.18,.18);
 if(uScene==12){
  vec3 low=uVariant==2?vec3(.34,.12,.045):vec3(.65,.3,.105);
  vec3 high=uVariant==5?vec3(.012,.025,.065):vec3(.025,.12,.27);
  vec3 c=mix(low,high,pow(horizon,.65));
  float clouds=smoothstep(.46,.78,fbm(sky*.75+vec2(uTime*.003,0)));
  c=mix(c,vec3(.42,.4,.36),clouds*smoothstep(.01,.25,ray.y)*.65);
  float haze=pow(max(0.,1.-abs(ray.y)),12.);
  c+=haze*vec3(.12,.065,.025);
  float sun=pow(max(dot(ray,normalize(vec3(-.6,.35,-.7))),0.),95.);
  return c+sun*vec3(1.9,1.25,.55);
 }
 if(uScene==13){
  vec3 low=uVariant==3?vec3(.045,.085,.12):vec3(.008,.034,.024);
  vec3 high=uVariant==5?vec3(.065,.008,.085):vec3(.008,.006,.025);
  float canopy=fbm(ray.xz*5.+vec2(uTime*.002,1.));
  float shafts=pow(max(0.,sin(ray.x*37.+ray.z*11.+canopy*2.)),14.);
  float glow=pow(max(0.,1.-abs(ray.y-.16)),7.);
  return mix(low,high,horizon)+glow*vec3(.012,.06,.035)+shafts*glow*vec3(.07,.13,.075);
 }
 if(uScene==14){
  vec2 starCoord=ray.xy/(abs(ray.z)+.25)*370.;
  float star=step(.996,hash(floor(starCoord)))*pow(max(0.,1.-length(fract(starCoord)-.5)*2.),4.);
  vec3 cosmos=mix(vec3(.004,.005,.018),vec3(.028,.009,.055),horizon);
  float dust=fbm(ray.xz*4.+ray.y*2.);
  float nebula=pow(max(0.,1.-abs(ray.y+.25*ray.x+(dust-.5)*.45)),8.);
  cosmos+=nebula*mix(vec3(.025,.035,.09),vec3(.12,.015,.13),dust)+star*vec3(.7,.85,1.);
  vec3 planetDirection=normalize(vec3(-.7,.28,-1.));
  float disc=smoothstep(.979,.980,dot(ray,planetDirection));
  float bands=.7+.3*sin(ray.y*180.+noise(ray.xz*55.)*2.);
  vec3 planet=vec3(.065,.045,.11)*bands+vec3(.08,.055,.15)*pow(max(0.,dot(ray,normalize(vec3(-.9,.4,-1.)))),35.);
  return mix(cosmos,planet,disc);
 }
 if(uVariant>=6){
  float sun=pow(max(dot(ray,normalize(vec3(0,.18,-1.))),0.),110.);
  float cloud=fbm(sky*.6+vec2(0,uTime*.001));
  return mix(vec3(.45,.2,.1),vec3(.025,.075,.16),horizon)+sun*vec3(2.,1.15,.4)+cloud*vec3(.045,.035,.035);
 }
 if(uVariant==5)return mix(vec3(.055,.08,.16),vec3(.35,.32,.24),horizon);
 float curtain=pow(max(0.,sin(ray.x*8.+ray.z*3.+fbm(ray.xz*3.)*3.)),5.);
 return mix(vec3(.015,.025,.065),vec3(.065,.075,.17),horizon)+curtain*vec3(.025,.1,.11)*pow(max(0.,1.-abs(ray.y-.35)),8.);
}
vec3 emissionColor(){
 if(uScene==12)return uVariant==5?vec3(1.8,.65,.12):vec3(.12,1.1,1.65);
 if(uScene==13)return uVariant==3?vec3(.6,1.3,.9):mix(vec3(.12,1.5,.65),vec3(1.2,.25,1.5),float(uVariant)/5.);
 if(uScene==14)return mix(vec3(.35,.65,1.8),vec3(1.3,.25,.9),uChapterProgress*.5);
 if(uVariant>=5)return vec3(1.8,1.2,.42);
 return mix(vec3(.1,1.2,1.8),vec3(1.8,.4,.18),float(uVariant)*.13);
}
vec2 surfaceUV(vec3 p,vec3 n){
 vec3 a=abs(n);
 if(a.y>a.x&&a.y>a.z)return p.xz;
 if(a.x>a.z)return p.zy;
 return p.xy;
}
float etchedGrid(vec2 uv,float spacing,float width,float footprint){
 vec2 edge=abs(fract(uv/spacing+.5)-.5)*spacing;
 return 1.-smoothstep(width,width+footprint,min(edge.x,edge.y));
}
float veinPattern(vec2 uv,float footprint){
 float mainVein=abs(sin(uv.x*1.7+.3*sin(uv.y*.8)))/1.7;
 float ribs=abs(sin(uv.y*7.+abs(uv.x)*3.))/7.;
 float vein=min(mainVein,ribs);
 return 1.-smoothstep(.015,.015+footprint,vein);
}
vec3 surfaceColor(vec3 p,vec3 n,float material,float footprint){
 vec2 uv=surfaceUV(p,n);
 float weather=fbm(uv*.7),fine=noise(uv*19.);
 float grain=mix(.85+fine*.3,1.,smoothstep(.02,.1,footprint));
 float panel=etchedGrid(uv,2.,.025,footprint);
 if(uScene==12){
  if(material<.5)return mix(vec3(.22,.085,.025),vec3(.57,.3,.1),weather)*grain;
  // Machined armour (1.4): smooth plating without the monument tile grid.
  if(material>1.2&&material<1.5)return vec3(.6,.55,.45)*(.76+.32*weather)*grain;
  vec3 ceramic=vec3(.58,.53,.42)*(.7+.4*weather)*grain;
  vec3 basalt=vec3(.075,.042,.023)*(.55+.7*weather);
  return mix(material<1.5?ceramic:basalt,vec3(.018,.025,.028),panel*.72);
 }
 if(uScene==13){
  if(material<.5)return mix(vec3(.008,.017,.012),vec3(.025,.07,.04),weather)*grain;
  float veins=veinPattern(uv,footprint);
  vec3 leaf=mix(vec3(.035,.29,.08),vec3(.38,.065,.24),float(uVariant)*.14);
  vec3 bark=vec3(.018,.09,.035)*(.5+weather);
  return mix(material<1.5?bark:leaf*(.65+.55*weather),vec3(.16,.48,.16),veins*.38)*grain;
 }
 if(uScene==14){
  float strata=.6+.4*sin(uv.y*5.+weather*4.);
  vec3 rock=(material<1.5?vec3(.105,.07,.17):vec3(.22,.07,.17))*(.65+.6*weather);
  return mix(rock,vec3(.012,.018,.04),panel*.65)*grain*(.8+.2*strata);
 }
 if(material<.5){
  if(uVariant>=6)return vec3(.018,.05,.095)*(.85+.2*weather);
  return mix(vec3(.012,.024,.04),vec3(.065,.105,.13),weather)*(1.-panel*.6)*grain;
 }
 // Wing membrane (2.4): iridescent bands radiating from the body, no tiles.
 if(material>2.2&&material<2.5)return mix(vec3(.06,.32,.52),vec3(.55,.08,.36),.5+.5*sin(length(p.xz)*1.25+weather*2.))*(.7+.45*weather)*grain;
 // Smoky chitin (1.4) for the finale's living forms.
 if(material>1.2&&material<1.5)return mix(vec3(.05,.045,.075),vec3(.16,.13,.2),weather)*grain;
 if(material<1.5)return mix(vec3(.48,.54,.65)*(.75+.35*weather),vec3(.02,.07,.1),panel*.8)*grain;
 vec3 glass=mix(vec3(.075,.3,.47),vec3(.48,.085,.14),.5+.5*sin(p.y*.35+float(uVariant)));
 return glass*(.7+.5*weather)*grain*(1.-panel*.45);
}
float softVisibility(vec3 p,vec3 lightDirection){
 float visibility=1.,travel=.09;
 for(int i=0;i<8;++i){
  float d=sceneField(p+lightDirection*travel).x;
  visibility=min(visibility,12.*d/travel);
  travel+=min(clamp(d,.09,2.),sceneBoundaryStep(p+lightDirection*travel,lightDirection));
  if(d<.003||travel>14.)break;
 }
 return clamp(visibility,.15,1.);
}
vec3 renderWorld(vec3 eye,vec3 ray,out float visibleDistance){
 float travel=.025,halo=0.;vec2 hitData=vec2(180.,0.);bool didHit=false;
 for(int i=0;i<112;++i){
  if(uRetro==1&&i>=72)break;
  vec3 p=eye+ray*travel;hitData=sceneField(p);
  float stepLength=min(clamp(hitData.x*.7,.009,4.5),sceneBoundaryStep(p,ray));
  if(hitData.y>2.5)halo+=exp(-abs(hitData.x)*9.)*.025*stepLength;
  if(hitData.x<.003){didHit=true;break;}
  travel+=stepLength;
  if(travel>150.)break;
 }
 visibleDistance=didHit?travel:150.;
 vec3 background=atmosphere(ray),c=background;
 if(didHit){
  vec3 p=eye+ray*travel,n=fieldNormal(p),lightDirection=normalize(vec3(-.55,.8,.3));
  if(uScene==13)lightDirection=normalize(vec3(.4,.7,-.3));
  float diffuse=max(dot(n,lightDirection),0.);
  float visibility=softVisibility(p+n*.02,lightDirection);
  float ao=clamp(sceneField(p+n*.15).x/.15*.6+sceneField(p+n*.4).x/.4*.4,.25,1.);
  float footprint=clamp(travel/max(uResolution.y,1.)*.8,.003,.35);
  vec2 uv=surfaceUV(p,n);
  vec3 albedo=surfaceColor(p,n,hitData.y,footprint),reflected=atmosphere(reflect(ray,n));
  float fresnel=pow(1.-max(dot(-ray,n),0.),4.);
  float roughness=noise(uv*3.);
  float specular=pow(max(dot(reflect(-lightDirection,n),-ray),0.),mix(35.,115.,roughness));
  vec3 key=uScene==12?vec3(1.15,.95,.7):vec3(.8,.95,1.1);
  vec3 rimColor=uScene==13?vec3(.1,.3,.18):vec3(.12,.22,.4);
  float rim=pow(max(dot(n,normalize(vec3(.6,.35,-.7))),0.),2.);
  c=albedo*(.21*ao+diffuse*visibility*1.05)*key+reflected*(.08+fresnel*.4)+specular*visibility*vec3(.7,.8,1.)*.8;
  c+=rimColor*rim*(.35+.45*fresnel);
  // Traveling energy has a visible path across the architecture, rather than
  // uniformly flashing the frame. Kick lights the power bus, snare launches a
  // short bright packet and high frequencies reveal fine conductive detail.
  float scan=.5+.5*sin(p.y*1.15+p.z*.28-uChapterTime*7.);
  float packet=pow(scan,12.);
  float pulse=responseScale()*(kickResponse()*1.35+snapResponse()*.65);
  if(hitData.y>2.5)c=emissionColor()*(1.1+pulse+packet*(.45+highResponse()*.7)+uEvent*.6);
  if(hitData.y>.5&&hitData.y<2.5){
   float edgeLight=pow(1.-max(dot(-ray,n),0.),3.);
   c+=emissionColor()*edgeLight*responseScale()*(.08+.2*kickResponse())*ao;
  }
  if(uScene==13&&hitData.y<2.5){
   float veins=veinPattern(uv,footprint);
   c+=emissionColor()*veins*(hitData.y<.5?.07:.26)*(.6+pulse*.8+packet*.65);
   // Thin leaves transmit warm light through their shaded faces.
   float transmitted=pow(max(dot(-n,lightDirection),0.),2.)*(.4+.6*max(dot(ray,-lightDirection),0.));
   if(hitData.y>1.5)c+=albedo*vec3(.65,1.2,.55)*transmitted*.75;
  }
  if((uScene==12||uScene==15)&&hitData.y>.5&&hitData.y<1.2){
   // Fine conductive seams and inset fasteners give white masses readable scale.
   float seam=etchedGrid(uv,2.,.012,footprint);
   vec2 bolt=abs(fract(uv*.5+.5)-.5)*2.-vec2(.16);
   float rivet=1.-smoothstep(.045,.045+footprint,length(bolt));
   c+=emissionColor()*seam*(.12+responseScale()*(kickResponse()*.22+packet*.3))+vec3(.12,.16,.2)*rivet;
  }
  if(uScene==14&&hitData.y<2.5){
   float fault=etchedGrid(uv,3.,.023,footprint);
   c+=emissionColor()*fault*(.1+packet*.45+pulse*.35);
  }
  if(hitData.y<.5&&(uScene==15&&uVariant>=6||uScene==13&&uVariant==3)){
   float ripple=.5+.5*sin(length(p.xz)*2.5-uChapterTime*1.7);
   c+=reflected*fresnel*.4+emissionColor()*.03*ripple;
  }
  float density=uScene==13?.018:(uScene==14?.005:.011);
  if(uScene==15&&uVariant==5)density=.004;
  c=mix(background,c,exp(-travel*density));
 }
 return c+emissionColor()*min(halo,.45);
}

// Secondary traffic lives in world space and is depth-tested against the main
// march. Analytic capsules add near/far parallax without putting 24 extra
// objects into every normal, shadow and distance-field evaluation. Positions
// wrap only at the volume boundary, where their contribution fades to zero.
vec3 motionLayers(vec3 eye,vec3 ray,float visibleDistance){
 vec3 sum=vec3(0.);
 vec3 extent=vec3(56.,34.,76.);
 float quiet=responseScale();
 if(uScene==15&&uVariant>=6)quiet*=1.-smoothstep(.1,.8,uAction);
 for(int i=0;i<24;++i){
  float index=float(i),group=floor(index/3.);
  vec3 seed=vec3(hash(vec2(group+1.,7.)),hash(vec2(group+1.,13.)),hash(vec2(group+1.,23.)));
  vec3 velocity,source;
  float tailLength,width;
  if(uScene==12){
   // Three-point formations race over the giants and through canyon airspace.
   float wing=mod(index,3.)-1.;
   source=seed*extent+vec3(wing*2.1,6.+abs(wing)*.35,abs(wing)*2.2);
   velocity=vec3(-8.,.1,-11.);tailLength=1.8;width=.065;
  }else if(uScene==13){
   // A luminous pollen current spirals upward between leaves and seed gates.
   seed=vec3(hash(vec2(index+3.,7.)),hash(vec2(index+3.,13.)),hash(vec2(index+3.,23.)));
   source=seed*extent+vec3(sin(uChapterTime*.8+index)*1.4,0.,cos(uChapterTime*.65+index));
   velocity=vec3(.7,2.4,-2.);tailLength=.22;width=.085;
  }else if(uScene==14){
   // Counter-moving material fragments make the vertical gravity reversal legible.
   seed=vec3(hash(vec2(index+4.,7.)),hash(vec2(index+4.,13.)),hash(vec2(index+4.,23.)));
   source=seed*extent;
   velocity=vec3((seed.x-.5)*2.,mod(index,3.)<1.?-2.5:7.,-1.5);tailLength=.55;width=.11;
   // Action is irreversible, but avoid action-scaled time which teleports debris.
   source.y+=uAction*4.;
  }else{
   // Ascending light traffic gives cathedral scale; the final horizon clears.
   seed=vec3(hash(vec2(index+6.,7.)),hash(vec2(index+6.,13.)),hash(vec2(index+6.,23.)));
   source=seed*extent;
   velocity=uVariant==5?vec3(0.,-12.,0.):vec3(.8,7.,-3.);
   tailLength=uVariant==5?3.2:1.15;width=.055;
  }
  vec3 relative=mod(source+velocity*uChapterTime-eye+extent*.5,extent)-extent*.5;
  float boundary=min(min(.5-abs(relative.x)/extent.x,.5-abs(relative.y)/extent.y),.5-abs(relative.z)/extent.z);
  float fade=smoothstep(0.,.1,boundary);
  vec3 head=eye+relative;
  vec3 axis=normalize(velocity);
  float segment=tailLength*(1.+kickResponse()*.65);
  vec3 offset=head-eye;
  float parallel=dot(ray,axis),denom=max(1.-parallel*parallel,.015);
  float along=clamp((parallel*dot(ray,offset)-dot(axis,offset))/denom,-segment,0.);
  vec3 closest=head+axis*along-eye;
  float depth=dot(ray,closest);
  if(depth<=.35||depth>=visibleDistance)continue;
  float distanceToRay=length(closest-ray*depth);
  float radius=width+depth/max(uResolution.y,1.)*.7;
  float core=exp(-distanceToRay*distanceToRay/(radius*radius));
  float glow=exp(-distanceToRay*distanceToRay/(radius*radius*12.))*.16;
  float brightness=(.65+kickResponse()*.7+highResponse()*.5)*fade*quiet;
  vec3 tint=mix(emissionColor(),vec3(1.1,.7,.3),seed.x*.3);
  sum+=(tint*glow+mix(tint,vec3(1.8),.45)*core)*brightness;
 }
 return sum;
}

void cameraDirection(out vec3 eye,out vec3 target,out float focal){
 float progress=uChapterProgress;
 focal=1.55;target=vec3(0,2.,0);
 if(uScene==12){
  if(uVariant==0){eye=vec3(13.-progress*6.,2.4+progress*3.,20.-progress*5.);target=vec3(0,2.5+progress*2.,0);}
  else if(uVariant==1){eye=vec3(11.5,1.1+progress*2.,9.-progress*9.);target=vec3(0,2.3,-progress*7.);focal=1.45;}
  else if(uVariant==2){float z=8.-uChapterTime*15.;eye=vec3(sin(z*.075)*2.2,1.05,z);target=vec3(sin((z-12.)*.075)*2.2,1.5,z-12.);focal=1.2;}
  else if(uVariant==3){eye=vec3(14.-progress*5.,5.+progress*5.,20.-progress*3.);target=vec3(0,3.5,0);}
  else if(uVariant==4){eye=vec3(10.-progress*4.,2.5+progress*2.,12.-uChapterTime*8.);target=vec3(0,2.8,eye.z-8.);focal=1.4;}
  else{float launch=smoothstep(.15,.9,uAction);eye=vec3(19.-progress*4.,7.+progress*13.,24.-progress*10.);target=vec3(0,3.+launch*launch*30.,0);focal=1.3;}
 }else if(uScene==13){
  if(uVariant==0){eye=vec3(7.-progress*2.,2.2+progress*3.,12.-progress*3.);target=vec3(0,.3+development()*3.,0);}
  else if(uVariant==1){eye=vec3(8.-progress*2.,2.+progress*8.,11.-progress*3.);target=vec3(0,1.+progress*9.,0);focal=1.4;}
  else if(uVariant==2){float z=8.-uChapterTime*17.;eye=vec3(sin(z*.1)*.6,0.,z);target=vec3(0,0.,z-12.);focal=1.2;}
  else if(uVariant==3){eye=vec3(1.8-progress*.9,3.4,12.-progress*1.5);target=vec3(0,0.,0);focal=1.8;}
  else if(uVariant==4){eye=vec3(20.-progress*10.,16.+progress*3.,22.-progress*8.);target=vec3(0,2.,0);focal=1.45;}
  else{eye=vec3(3.,8.-progress*2.,18.+progress*4.);target=vec3(0,3.4,0);focal=1.45;}
 }else if(uScene==14){
  if(uVariant==0){eye=vec3(20.-progress*7.,12.+progress*5.,25.-progress*6.);target=vec3(0,-1.+uAction*4.,0);focal=1.4;}
  else if(uVariant==1){float z=24.-progress*88.;eye=vec3(0,12.+z*.375,z);target=vec3(sin(floor((z-15.)/8.)*1.7)*4.,5.+(z-15.)*.375,z-15.);focal=1.2;}
  else if(uVariant==2){eye=vec3(3.-progress*3.,8.-progress*4.,23.-progress*12.);target=vec3(0,0,0);focal=1.4;}
  else if(uVariant==3){eye=vec3(18.-progress*5.,6.+progress*10.,25.-progress*4.);target=vec3(0,3.+uAction*8.,0);focal=1.45;}
  else{eye=vec3(sin(progress*PI)*1.5,2.,17.-progress*15.);target=vec3(0,2.,-25.);focal=1.25;}
 }else{
  if(uVariant==0){eye=vec3(sin(progress*PI)*1.5,1.2+progress*1.3,22.-uChapterTime*7.5);target=eye+vec3(0,1.,-12.);focal=1.3;}
  else if(uVariant==1){eye=vec3(3.-progress*3.,10.-progress*4.,18.-progress*3.);target=vec3(0,3.,0);focal=1.55;}
  else if(uVariant==2){eye=vec3(4.-progress*4.,5.+progress*3.,24.-progress*6.);target=vec3(0,4.,0);focal=1.5;}
  else if(uVariant==3){eye=vec3(26.-progress*12.,20.+progress*5.,28.-progress*10.);target=vec3(0,3.,0);focal=1.25;}
  else if(uVariant==4){eye=vec3(2.,5.+progress*4.,19.+progress*15.);target=vec3(0,3.,0);focal=1.35;}
  else if(uVariant==5){float y=-5.+pow(progress,1.22)*130.;eye=vec3(1.25,y,1.5);target=vec3(0,y+13.,-.5);focal=1.06;}
  else{eye=vec3(.3-progress*.3,3.2,13.+progress*4.);target=vec3(0,4.,-15.);focal=1.7;}
 }
}
void main(){
 vec2 p=(vUV-.5)*vec2(uResolution.x/max(uResolution.y,1.),1.);
 vec3 eye,target;float focal;cameraDirection(eye,target,focal);
 bool quiet=uScene==13&&uVariant==3||uScene==15&&uVariant>=6;
 if(uScene==12&&uVariant==2||uScene==13&&uVariant==2)p=rot(sin(uChapterTime*.45)*.1)*p;
 bool travel=uScene==12&&(uVariant==2||uVariant==4)||uScene==13&&uVariant==2||uScene==14&&(uVariant==1||uVariant==4)||uScene==15&&(uVariant==0||uVariant==5);
 cinematicRig(eye,target,p,focal,quiet?.35:(travel?.1:.9),quiet?.2:(travel?.9:.6));
 vec3 ray=cameraBasis(eye,target)*normalize(vec3(p,focal));
 float visibleDistance;
 vec3 c=renderWorld(eye,ray,visibleDistance)*(1.+.025*uCut);
 c+=motionLayers(eye,ray,visibleDistance);
 // One accent flash announces the irreversible action, without washing out it.
 c+=emissionColor()*uEvent*.025;
 if(travel)c+=speedDust(p,focal,emissionColor()*.5+.15);
 color=vec4(max(c,vec3(0)),1.);
}
