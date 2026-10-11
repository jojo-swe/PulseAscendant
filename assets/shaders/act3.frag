#version 330 core
in vec2 vUV;
out vec4 color;
uniform float uTime,uEnergy;
uniform vec2 uResolution;
uniform int uScene,uRetro;
#include "common.glsl"

// The OpenGL fallback uses signed-distance geometry. Hardware tracing is a
// separate backend; this shader does not claim triangle RT or path tracing.
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
vec2 nearer(vec2 a,float distanceValue,float material){return distanceValue<a.x?vec2(distanceValue,material):a;}
float desertHeight(vec2 p){return -1.8+.32*sin(p.x*.13+p.y*.035)+.22*sin(p.y*.19-p.x*.04);}

vec2 desertWorld(vec3 p){
 vec2 h=vec2((p.y-desertHeight(p.xz))*.85,0.);
 // A large articulated ceramic walker is the persistent focal object.
 vec3 body=p-vec3(0,2.25+.09*sin(uTime*2.),0);
 h=nearer(h,solidBox(body,vec3(2.45,.9,1.25))-.13,1.);
 h=nearer(h,solidBox(p-vec3(0,3.75,0),vec3(1.05,.65,.8))-.12,1.);
 h=nearer(h,solidBox(p-vec3(0,3.88,.87),vec3(.82,.085,.045)),3.);
 h=nearer(h,solidBox(body-vec3(0,.02,1.4),vec3(1.65,.55,.11)),2.);
 for(int i=0;i<4;++i){
  float side=i<2?-1.:1.,front=(i==0||i==2)?-1.:1.;
  float phase=uTime*(1.55+.25*uDrive)+float(i)*PI*.5;
  vec3 hip=vec3(side*1.8,1.65,front*.83);
  vec3 knee=vec3(side*(2.2+.16*sin(phase)),.05,front*.83+.43*sin(phase));
  vec3 foot=vec3(side*2.25,0,front*.83+.62*cos(phase));
  foot.y=desertHeight(foot.xz)+.17+.24*max(sin(phase),0.);
  h=nearer(h,capsule(p,hip,knee,.24),1.);
  h=nearer(h,capsule(p,knee,foot,.19),2.);
  h=nearer(h,solidBox(p-foot,vec3(.34,.16,.55))-.04,1.);
 }
 // Repeated distant sentinels establish scale without competing with the hero.
 vec3 q=p; q.xz=mod(q.xz+vec2(10.,12.),vec2(20.,24.))-vec2(10.,12.);
 if(length(p.xz)>10.){
  float base=desertHeight(p.xz);
  h=nearer(h,solidBox(q-vec3(0,base+2.7,0),vec3(1.8,.65,.85))-.08,1.);
  h=nearer(h,solidBox(vec3(abs(q.x)-1.25,q.y-base-1.15,q.z),vec3(.22,1.35,.55)),2.);
  h=nearer(h,solidBox(q-vec3(0,base+3.8,0),vec3(.6,.45,.55)),1.);
 }
 return h;
}

vec2 gardenWorld(vec3 p){
 vec2 h=vec2((p.y+1.75+.17*sin(p.x*.3)*sin(p.z*.25))*.9,0.);
 vec2 cell=floor((p.xz+6.)/12.);
 vec3 q=p;q.xz=mod(q.xz+6.,12.)-6.;
 float seed=hash(cell+17.),growth=.88+.12*sin(uTime*.16+seed*3.)+.08*uBass;
 float height=(4.8+seed*1.7)*growth;
 q.x+=sin(uTime*.55+q.y*.32+seed*6.)*.14*clamp((q.y+1.5)/7.,0.,1.);
 h=nearer(h,capsule(q,vec3(0,-1.8,0),vec3(0,height,0),.26),1.);
 for(int level=0;level<3;++level){
  float y=(.4+float(level)*1.45)*growth,sector=2.*PI/7.;
  float angle=atan(q.z,q.x)+float(level)*.43+seed*3.;
  float folded=mod(angle+sector*.5,sector)-sector*.5;
  vec3 branch=vec3(cos(folded)*length(q.xz),q.y,sin(folded)*length(q.xz));
  float reach=(3.5-float(level)*.62)*growth+.15*sin(uTime*.7+float(level));
  vec3 tip=vec3(reach,y+1.45*growth+.17*sin(uTime*.8+seed),0);
  h=nearer(h,capsule(branch,vec3(0,y,0),tip,.115),1.);
  vec3 leaf=branch-tip;leaf.xy=rot(.45)*leaf.xy;
  float leafDistance=(length(leaf/vec3(.85,.33,.48))-1.)*.33;
  h=nearer(h,leafDistance,2.);
  h=nearer(h,capsule(branch,tip,tip+vec3(.45,.5,0),.035),3.);
 }
 // Seeds drift through three dimensions independently of the branch movement.
 vec3 drift=vec3(uTime*.23,-uTime*.49,sin(uTime*.13)*.5);
 vec3 mote=mod(p+drift+vec3(1.6,1.2,1.6),vec3(3.2,2.4,3.2))-vec3(1.6,1.2,1.6);
 h=nearer(h,length(mote)-(.045+.025*treble()),3.);
 return h;
}

vec2 gravityWorld(vec3 p){
 vec2 h=vec2(180.,0.);
 vec3 core=p-vec3(0,1.1,0);
 core.xz=rot(uTime*.16)*core.xz;core.xy=rot(uTime*.11)*core.xy;
 float rock=octahedron(core,3.15);
 rock=max(rock,-abs(core.x+.25*sin(core.z*1.1))+.11);
 rock=max(rock,-abs(core.y+.18*sin(uTime))+.09);
 h=nearer(h,rock,1.);
 float sector=2.*PI/9.,angle=atan(p.z,p.x)-uTime*.13;
 float folded=mod(angle+sector*.5,sector)-sector*.5;
 float cellAngle=floor((angle+sector*.5)/sector)*sector+uTime*.13;
 vec3 chunk=vec3(cos(folded)*length(p.xz)-6.7,p.y-1.25-.85*sin(cellAngle*2.+uTime*.7),sin(folded)*length(p.xz));
 chunk.xy=rot(.18*sin(uTime*.6+cellAngle))*chunk.xy;
 float platform=solidBox(chunk,vec3(1.45,.6,1.85));
 platform=max(platform,(abs(chunk.x)+abs(chunk.z)-2.5)*.7071);
 h=nearer(h,platform,1.);
 h=nearer(h,solidBox(chunk-vec3(0,.64,0),vec3(.92,.025,1.25)),3.);
 if(length(p.xz)>19.5){
  vec3 cell=floor((p+vec3(2.5,2.,2.5))/vec3(5.,4.,5.));
  vec3 pebble=mod(p+vec3(2.5,2.,2.5),vec3(5.,4.,5.))-vec3(2.5,2.,2.5);
  float seed=hash(cell.xz+cell.y*13.);
  pebble.y+=sin(uTime*.7+seed*6.)*.65;
  pebble.xz=rot(uTime*(.14+seed*.2))*pebble.xz;
  h=nearer(h,octahedron(pebble,.35+seed*.65),2.);
 }
 vec3 orbit=p-vec3(0,1.1,0);orbit.xy=rot(.45)*orbit.xy;
 h=nearer(h,ringShape(orbit,4.2,.045+.025*uBass),3.);
 return h;
}

vec2 finaleWorld(vec3 p){
 vec2 h=vec2(p.y+1.8,0.);
 // Corridor ribs become an open monumental arch rather than an enclosed room.
 vec3 arch=p;arch.z=mod(arch.z+5.,10.)-5.;
 float radius=length(vec2(arch.x*.72,arch.y+1.8));
 float arc=max(abs(radius-7.)-.15,abs(arch.z)-.28);
 arc=max(arc,-arch.y-1.8);
 h=nearer(h,arc,1.);
 vec3 gem=p-vec3(0,2.6,0);gem.xz=rot(uTime*.24)*gem.xz;
 gem.yz=rot(.18*sin(uTime*.35))*gem.yz;
 h=nearer(h,octahedron(gem,3.3+.16*uBass),2.);
 vec3 cage=gem;float edge=abs(octahedron(cage,4.1))-.035;
 edge=max(edge,min(abs(cage.x),min(abs(cage.y),abs(cage.z)))-.065);
 h=nearer(h,edge,3.);
 float angle=atan(p.z,p.x)-uTime*.12,sector=2.*PI/16.;
 float folded=mod(angle+sector*.5,sector)-sector*.5;
 vec3 sail=vec3(cos(folded)*length(p.xz)-7.,p.y-2.7-.6*sin(angle*2.+uTime),sin(folded)*length(p.xz));
 sail.xy=rot(.22)*sail.xy;
 h=nearer(h,octahedron(sail/vec3(.85,2.1,.65),1.05)*.65,2.);
 vec3 halo=p-vec3(0,2.6,0);halo.yz=rot(.35*sin(uTime*.21))*halo.yz;
 h=nearer(h,ringShape(halo,5.2,.055),3.);
 return h;
}

vec2 sceneField(vec3 p){
 if(uScene==12)return desertWorld(p);
 if(uScene==13)return gardenWorld(p);
 if(uScene==14)return gravityWorld(p);
 return finaleWorld(p);
}
vec3 fieldNormal(vec3 p){
 const vec2 e=vec2(.003,-.003);
 vec3 n=e.xyy*sceneField(p+e.xyy).x+e.yyx*sceneField(p+e.yyx).x+
        e.yxy*sceneField(p+e.yxy).x+e.xxx*sceneField(p+e.xxx).x;
 return n/max(length(n),.00001);
}
vec3 atmosphere(vec3 ray){
 float horizon=clamp(ray.y*.5+.5,0.,1.);
 if(uScene==12){
  vec3 c=mix(vec3(.74,.35,.12),vec3(.13,.39,.65),pow(horizon,.7));
  return c+vec3(1.7,1.2,.65)*pow(max(dot(ray,normalize(vec3(-.5,.65,-.6))),0.),110.);
 }
 if(uScene==13)return mix(vec3(.025,.07,.07),vec3(.075,.025,.13),horizon)+vec3(.025,.06,.035)*pow(1.-abs(ray.y),6.);
 if(uScene==14){
  vec2 starCoord=ray.xy/(abs(ray.z)+.25)*370.;
  float star=step(.996,hash(floor(starCoord)))*pow(max(0.,1.-length(fract(starCoord)-.5)*2.),4.);
  return mix(vec3(.015,.012,.04),vec3(.065,.025,.12),horizon)+star*vec3(.7,.85,1.);
 }
 return mix(vec3(.04,.055,.12),vec3(.16,.1,.24),horizon)+vec3(.1,.19,.3)*pow(max(ray.y,0.),3.);
}
vec3 emissionColor(){
 if(uScene==12)return vec3(.08,1.3,1.8);
 if(uScene==13)return mix(vec3(.1,1.4,.65),vec3(.8,.25,1.4),treble());
 if(uScene==14)return vec3(.35,.75,1.8);
 return mix(vec3(.1,1.2,1.8),vec3(1.8,.45,.12),.5+.5*sin(uTime*.18));
}
vec3 surfaceColor(vec3 p,float material){
 if(uScene==12){
  if(material<.5)return mix(vec3(.56,.25,.08),vec3(.84,.53,.22),.5+.5*sin(p.x*4.+p.z*.4));
  return material<1.5?vec3(.82,.79,.65):vec3(.12,.075,.045);
 }
 if(uScene==13){
  if(material<.5)return vec3(.025,.055,.045);
  return material<1.5?vec3(.045,.28,.13):vec3(.12,.48,.22);
 }
 if(uScene==14)return material<1.5?vec3(.14,.1,.23):vec3(.32,.18,.25);
 if(material<.5)return vec3(.055,.08,.13);
 return material<1.5?vec3(.68,.72,.83):mix(vec3(.18,.48,.8),vec3(.75,.24,.36),.5+.5*sin(p.y*1.4+uTime*.3));
}
float softVisibility(vec3 p,vec3 lightDirection){
 float visibility=1.,travel=.08;
 for(int i=0;i<10;++i){
  float d=sceneField(p+lightDirection*travel).x;
  visibility=min(visibility,12.*d/travel);
  travel+=clamp(d,.08,1.8);
  if(d<.002||travel>16.)break;
 }
 return clamp(visibility,.12,1.);
}
vec3 renderWorld(vec3 eye,vec3 ray){
 float travel=.02,halo=0.;vec2 hitData=vec2(180.,0.);bool didHit=false;
 for(int i=0;i<112;++i){
  vec3 p=eye+ray*travel;hitData=sceneField(p);
  float stepLength=clamp(hitData.x*.72,.009,4.);
  if(hitData.y>2.5)halo+=exp(-abs(hitData.x)*8.)*.035*stepLength;
  if(hitData.x<.0025){didHit=true;break;}
  travel+=stepLength;
  if(travel>145.)break;
 }
 vec3 background=atmosphere(ray),c=background;
 if(didHit){
  vec3 p=eye+ray*travel,n=fieldNormal(p),lightDirection=normalize(vec3(-.5,.85,.3));
  if(uScene==13)lightDirection=normalize(vec3(.4,.65,-.3));
  float diffuse=max(dot(n,lightDirection),0.);
  float visibility=softVisibility(p+n*.015,lightDirection);
  float ao=clamp(sceneField(p+n*.12).x/.12*.6+sceneField(p+n*.35).x/.35*.4,.2,1.);
  vec3 albedo=surfaceColor(p,hitData.y);
  vec3 reflected=atmosphere(reflect(ray,n));
  float fresnel=pow(1.-max(dot(-ray,n),0.),4.);
  float specular=pow(max(dot(reflect(-lightDirection,n),-ray),0.),uScene==12?35.:65.);
  c=albedo*(.17*ao+diffuse*visibility*.95)+reflected*(.12+fresnel*.42)+specular*visibility*vec3(.7,.8,.95);
  if(hitData.y>2.5)c=emissionColor()*(1.8+uHit*1.1+midrange()*.9);
  if(uScene==13&&hitData.y>.5&&hitData.y<2.5)c+=emissionColor()*.12*(.4+uBass);
  if(uScene==15&&hitData.y<.5){
   vec2 grid=abs(fract(p.xz*.25)-.5);
   c+=emissionColor()*pow(max(grid.x,grid.y)*2.,42.)*(.12+.3*midrange());
  }
  float density=uScene==13?.026:(uScene==14?.009:.013);
  c=mix(background,c,exp(-travel*density));
 }
 return c+emissionColor()*min(halo,.6);
}

void main(){
 vec2 p=(vUV-.5)*vec2(uResolution.x/max(uResolution.y,1.),1.);
 float a=uTime*.12+float(uShot)*PI*.5;
 vec3 eye,target=vec3(0,2.2,0);
 if(uScene==12){
  eye=vec3(sin(a)*9.,1.6,cos(a)*11.);
  if(uShot==1)eye=vec3(6.2,4.1,5.+sin(uTime*.4));
  if(uShot==2)eye=vec3(sin(a)*14.,8.,cos(a)*14.);
  if(uShot==3)eye=vec3(5.7+sin(uTime*.6),1.4,16.-uProgress*32.);
 }else if(uScene==13){
  eye=vec3(sin(a)*7.8,2.3+sin(uTime*.3)*.6,cos(a)*7.8);target=vec3(0,2.4,0);
  if(uShot==1)eye=vec3(sin(a)*5.2,4.4,cos(a)*5.2);
  if(uShot==2)eye=vec3(sin(a)*9.8,7.7,cos(a)*9.8);
  if(uShot==3){eye=vec3(5.+sin(uTime*.5)*.3,3.6,11.-uProgress*22.);target=vec3(0,3.,0);}
 }else if(uScene==14){
  eye=vec3(sin(a)*14.,3.5+sin(uTime*.4)*1.4,cos(a)*14.);target=vec3(0,1.1,0);
  if(uShot==1)eye=vec3(sin(a)*10.,.1,cos(a)*10.);
  if(uShot==2)eye=vec3(sin(a)*12.,10.5,cos(a)*12.);
  if(uShot==3)eye=vec3(9.5,3.8,16.-uProgress*32.);
  p=rot(sin(uTime*.15)*.18*uDrive)*p;
 }else{
  eye=vec3(sin(a)*13.,3.3,cos(a)*13.);target=vec3(0,2.6,0);
  if(uShot==0)eye=vec3(sin(uTime*.25)*2.5,1.8,19.-uProgress*8.);
  if(uShot==2)eye=vec3(sin(a)*15.,9.,cos(a)*15.);
  if(uShot==3)eye=vec3(9.5,3.7,19.-uProgress*36.);
 }
 p+=vec2(sin(uTime*29.),cos(uTime*23.))*uHit*.0017;
 vec3 ray=cameraBasis(eye,target)*normalize(vec3(p,1.5-.06*uDrive));
 vec3 c=renderWorld(eye,ray)*(1.+.06*uCut);
 color=vec4(max(c,vec3(0)),1);
}
