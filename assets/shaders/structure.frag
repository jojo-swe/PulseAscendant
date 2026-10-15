#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;
#include "common.glsl"
float box(vec3 p,vec3 b){vec3 q=abs(p)-b;return length(max(q,0.))+min(max(q.x,max(q.y,q.z)),0.);}
vec2 structure(vec3 p){
 float k=clamp(uChapterProgress,0.,1.),z=mod(p.z+6.,12.)-6.,cell=floor((p.z+6.)/12.);
 vec3 q=vec3(p.xy,z);float metal=100.,light=100.;
 if(uVariant==0){
  // Split monolithic gates: two halves separate and the keystone lifts once.
  float open=.4+k*.45+uAction*1.25;
  metal=box(vec3(abs(q.x)-(3.+open),q.y,q.z),vec3(.55,3.6,.6));
  metal=min(metal,box(vec3(q.x,q.y-3.1-open*.4,q.z),vec3(3.2+open,.4,.6)));
  light=box(vec3(abs(q.x)-(2.43+open),q.y,q.z),vec3(.045,3.0,.64));
  light=min(light,box(vec3(q.x,q.y+3.,q.z),vec3(3.1,.05,.63)));
 }else if(uVariant==1){
  // A low machine hall with piston towers, conveyor teeth and descending gantries.
  float piston=.7*uAction+k*.3+sin(uChapterTime*3.2+cell*.9)*.55+uPulse.x*.3;
  metal=box(vec3(abs(q.x)-4.,q.y+1.2,q.z),vec3(.65,3.1,1.2));
  metal=min(metal,box(vec3(q.x,q.y-3.2+piston*.5,q.z),vec3(4.3,.45,.75)));
  vec3 gear=q;gear.xy=rot(cell*.45+uChapterTime*1.25)*gear.xy;
  float teeth=box(vec3(abs(gear.x)-2.7,gear.y-3.5,gear.z),vec3(.45,.8,.5));metal=min(metal,teeth);
  float floorD=p.y+3.6;metal=min(metal,floorD);
  vec3 rail=vec3(abs(p.x)-1.9,p.y+3.3,0.);light=length(rail.xy)-.08;
 }else if(uVariant==2){
  // Successive fractured slabs peel outward in a wave; the exit is created on screen.
  float phase=smoothstep(.1,.85,k+sin(cell*.7)*.08)*.25+uAction*.75;
  vec3 g=q;g.xy=rot(cell*.6+phase*.7)*g.xy;
  float a=atan(g.y,g.x),sector=2.*PI/5.;a=mod(a+sector*.5,sector)-sector*.5;
  vec3 shard=vec3(cos(a)*length(g.xy)-(3.35+phase*2.5),sin(a)*length(g.xy),g.z);
  metal=box(shard,vec3(.36,1.45,.8));
  light=box(shard-vec3(-.37,0,0),vec3(.035,1.3,.84));
  vec3 brace=q;brace.xy=rot(cell*.25)*brace.xy;
  metal=min(metal,box(vec3(brace.x,abs(brace.y)-4.1-phase,brace.z),vec3(2.7,.12,.35)));
 }else{
  // A helix of triangular frames is pulled into long parallel vector rails.
  float unfold=uAction*.75+k*.25;
  vec3 g=q;g.xy=rot(cell*.48*(1.-unfold)+k*.3)*g.xy;
  float a=atan(g.y,g.x),sector=2.*PI/3.;a=mod(a+sector*.5,sector)-sector*.5;
  float edge=cos(a)*length(g.xy)-(2.6+unfold*.8);
  light=length(vec2(edge,g.z))-.11;
  metal=length(vec2(edge-.25,g.z))-.19;
  vec3 rails=p;rails.xy=rot(p.z*.045*(1.-unfold))*rails.xy;
  metal=min(metal,length(vec2(abs(rails.x)-3.6,rails.y+1.7))-.20);
  light=min(light,length(vec2(abs(rails.x)-3.6,rails.y+1.5))-.055);
 }
 return metal<light?vec2(metal,1.):vec2(light,2.);
}
vec3 normalAt(vec3 p){vec2 e=vec2(.004,0);return normalize(vec3(structure(p+e.xyy).x-structure(p-e.xyy).x,structure(p+e.yxy).x-structure(p-e.yxy).x,structure(p+e.yyx).x-structure(p-e.yyx).x));}
void main(){
 vec2 p=(vUV*2.-1.)*vec2(uResolution.x/uResolution.y,1.);float k=clamp(uChapterProgress,0.,1.);
 float speed=uVariant==1?19.:25.,z=uChapterTime*speed;
 float bank=uVariant==3?k*.35:sin(k*PI)*.08;
 p=rot(bank)*p;vec3 ro=vec3(sin(k*PI)*.5,uVariant==1?-.8:0.,z);
 vec3 target=ro+vec3(-sin(k*PI)*.2,uVariant==2?sin(k*PI)*.4:0.,20.);float lens=1.3;
 cinematicRig(ro,target,p,lens,0.,1.);
 vec3 rd=cameraBasis(ro,target)*normalize(vec3(p,lens));
 float t=.03,glow=0.;vec2 h;bool hit=false;
 for(int i=0;i<112;i++){
  h=structure(ro+rd*t);float ds=clamp(h.x*.65,.006,1.1);
  if(h.y>1.5)glow+=exp(-abs(h.x)*15.)*ds;
  if(h.x<.003){hit=true;break;}t+=ds;if(t>95.)break;
 }
 vec3 neon=accent(float(uVariant)+.5),c=vec3(.004,.008,.02);
 if(hit){vec3 at=ro+rd*t,n=normalAt(at),l=normalize(ro+vec3(2,3,1)-at);
  float spec=pow(max(dot(reflect(-l,n),-rd),0.),48.),edge=pow(1.-max(dot(n,-rd),0.),3.);
  float panel=step(.035,fract(at.y*1.5))*step(.035,fract(at.z*.75));
  float grain=.93+.07*sin(at.y*80.+at.x*40.);
  c=vec3(.085,.13,.19)*(.35+max(dot(n,l),0.))*(.55+.45*panel)*grain+vec3(.4,.65,.9)*spec;
  float runner=pow(.5+.5*cos(at.z*.7-uChapterTime*17.+at.y),20.);
  c+=neon*(edge*.28+float(h.y>1.5)*(.55+uBass*.3+uEvent*.9+uPulse.x*1.6+runner*.9));
  c+=vec3(.15,.3,.5)*line(at.z-(z+50.*(1.-k)),.6);
  c*=exp(-t*.023);
 }
 c+=neon*glow*(.07+uPulse.y*.10);c+=neon*.025*pow(max(rd.z,0.),50.);
 c+=speedDust(p,lens,neon*.6+.25);
 color=vec4(c,1.);
}
