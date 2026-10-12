#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;uniform float uEnergy;
#include "common.glsl"
// Five journeys, rather than five camera passes through the same tube.
float boxC(vec3 p,vec3 b){vec3 q=abs(p)-b;return length(max(q,0.))+min(max(q.x,max(q.y,q.z)),0.);}
vec2 centre(float z){float bend=uVariant==0?.3:(uVariant==2?1.1:.65);return vec2(sin(z*.055)*bend,sin(z*.037)*bend*.55);}
float section(vec2 q){
 if(uVariant==0)return length(q);
 if(uVariant==1)return max(abs(q.x)*.866025+abs(q.y)*.5,abs(q.y));
 if(uVariant==2)return max(abs(q.x)*.78,abs(q.y)*1.35);
 if(uVariant==3)return max(abs(q.x)*.85,-q.y*1.1);
 vec2 a=abs(q);return max(max(a.x,a.y),dot(a,vec2(.7071)));
}
vec2 corridor(vec3 p){
 vec2 q=p.xy-centre(p.z);float k=clamp(uChapterProgress,0.,1.);
 float exitOpen=smoothstep(.57,.92,k)*smoothstep(55.,110.,p.z);
 float wall=(4.1+exitOpen*8.-section(q))*.58;
 vec2 hit=vec2(wall,1.);
 float spacing=uVariant==0?16.:(uVariant==1?9.:(uVariant==2?23.:19.));
 float cell=floor((p.z+spacing*.5)/spacing),z=mod(p.z+spacing*.5,spacing)-spacing*.5;
 float opening=k*.5+uAction*1.2;
 vec3 g=vec3(q,z);
 if(uVariant==0){
  // Six massive launch doors peel outward in sequence as the craft passes.
  float a=atan(q.y,q.x),sector=2.*PI/6.;a=mod(a+sector*.5,sector)-sector*.5;
  g.xy=vec2(cos(a),sin(a))*length(q)-vec2(3.1+opening,0.);
  float gate=boxC(g,vec3(.27,1.4,.42));if(gate<hit.x)hit=vec2(gate,2.);
 }else if(uVariant==1){
  // A tight hexagonal accelerator, with a travelling shock front.
  float rib=length(vec2(section(q)-(3.6+opening*.2),z))-.12;
  if(rib<hit.x)hit=vec2(rib,2.);
  vec3 blade=vec3(q,z);blade.xy=rot(cell*.73+k*2.)*blade.xy;
  float fin=boxC(vec3(abs(blade.x)-3.0-opening*.25,blade.y,blade.z),vec3(.16,.95,.30));
  if(fin<hit.x)hit=vec2(fin,3.);
 }else if(uVariant==2){
  // A low, wide split-rail core dive with alternating ceiling turbines.
  float rails=length(vec2(abs(q.x)-2.5,q.y+2.4))-.13;
  float ceiling=boxC(vec3(q.x,q.y-2.4-opening*.3,z),vec3(1.55,.22,1.1));
  if(rails<hit.x)hit=vec2(rails,2.);if(ceiling<hit.x)hit=vec2(ceiling,3.);
 }else if(uVariant==3){
  // Open sky and a zig-zag trench; broad magnetic arms close behind us.
  float a=sin(cell*1.7)*.75;
  g.xy=rot(a*(.15+.85*smoothstep(.3,.85,k)))*g.xy;
  float arm=boxC(vec3(abs(g.x)-3.15,g.y-1.2,g.z),vec3(.25,1.8,.35));
  if(arm<hit.x)hit=vec2(arm,2.);
 }else{
  // Fragments of a broken octagonal tube separate into an open launch lane.
  g.xy=rot(cell*.31+k*.7)*g.xy;
  vec2 a=abs(g.xy);float shutter=boxC(vec3(a.x-(2.8+opening*1.2),g.y,z),vec3(.3,.8,.5));
  float spar=boxC(vec3(g.x,a.y-(2.8+opening*.6),z),vec3(1.7,.16,.24));
  if(min(shutter,spar)<hit.x)hit=vec2(min(shutter,spar),3.);
 }
 return hit;
}
vec3 wallNormal(vec3 p){vec2 e=vec2(.006,0);return normalize(vec3(corridor(p+e.xyy).x-corridor(p-e.xyy).x,corridor(p+e.yxy).x-corridor(p-e.yxy).x,corridor(p+e.yyx).x-corridor(p-e.yyx).x));}
void main(){
 vec2 p=(vUV*2.-1.)*vec2(uResolution.x/uResolution.y,1.);float k=clamp(uChapterProgress,0.,1.);
 // The camera travels toward an exit once. Its progress does not reset inside a chapter.
 float z=uChapterTime*(uVariant==2?9.:12.)+k*k*15.;
 vec3 ro=vec3(centre(z)+vec2(sin(k*PI)*.32,cos(k*PI)*.16),z);
 p=rot(sin(k*PI*2.)*(uVariant==3?.22:.09))*p;
 vec3 target=vec3(centre(z+24.),z+24.);vec3 rd=cameraBasis(ro,target)*normalize(vec3(p,1.5-uBass*.08));
 float t=.03,volume=0.;bool found=false;vec2 h=vec2(0.);
 for(int i=0;i<104;i++){
  vec3 at=ro+rd*t;h=corridor(at);float ds=clamp(h.x,.012,1.5);
  if(h.y>1.5)volume+=exp(-abs(h.x)*12.)*.05*ds*exp(-t*.035);
  if(h.x<.004){found=true;break;}t+=ds;if(t>90.)break;
 }
 vec3 c=vec3(.003,.008,.023);
 if(uVariant==3)c+=vec3(.025,.035,.08)*max(rd.y,0.);
 if(found){
  vec3 at=ro+rd*t,n=wallNormal(at);vec2 q=at.xy-centre(at.z);
  float angle=atan(q.y,q.x),ring=abs(mod(at.z+1.8,3.6)-1.8);
  float groove=line(sin(angle*(uVariant==1?3.:6.)),.018);
  vec3 emission=accent(angle*.4+float(uVariant));
  float band=frequencyBand((angle/PI+1.)*11.99);
  vec3 l=normalize(ro+vec3(1,2,2)-at);
  float spec=pow(max(dot(reflect(-l,n),-rd),0.),48.);
  c=vec3(.04,.065,.10)*(.25+max(dot(n,l),0.))+vec3(.25,.5,.75)*spec;
  c+=emission*(line(ring,.045)*.65+groove*.10)*(.35+band);
  if(h.y>1.5)c+=emission*(.8+uHit*.45+band*.6);
  // A non-looping pressure wave crosses the world, instead of endless equal flashes.
  c+=vec3(.55,.8,1.)*line(at.z-(z+70.*(1.-k)),1.2)*(1.+uEvent);
  c=mix(c,vec3(.01,.03,.065),1.-exp(-t*.025));
 }
 c+=accent(float(uVariant))*volume*.35;
 c+=vec3(.08,.3,.45)*pow(max(dot(rd,normalize(target-ro)),0.),90.)*(.15+.8*k*k);
 color=vec4(c,1.);
}
