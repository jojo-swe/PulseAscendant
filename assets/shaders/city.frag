#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;
#include "common.glsl"
float box(vec3 p,vec3 b){vec3 q=abs(p)-b;return length(max(q,0.))+min(max(q.x,max(q.y,q.z)),0.);}
vec2 city(vec3 p){
 float k=clamp(uChapterProgress,0.,1.);vec2 cell=floor((p.xz+4.)/8.),q=mod(p.xz+4.,8.)-4.;
 float seed=hash(cell),h=4.+seed*16.,b=100.,material=2.;
 float lane=abs(cell.x)<1.?0.:1.;
 if(uVariant==0){
  // Street-level neon towers grow in a wave ahead of the ascending camera.
  float build=smoothstep(.0,.8,k*.25+uAction*.75+(p.z-uChapterTime*24.)*.006);
  h*=.65+build*.5;b=box(vec3(q.x,p.y-h*.5,q.y),vec3(2.3,h*.5,2.4));
  float crown=box(vec3(q.x,p.y-h-.65,q.y),vec3(1.6,.65,1.7));b=min(b,crown);
 }else if(uVariant==1){
  // Terraced skyline: broad lower slabs, narrow upper towers and high bridges.
  h=9.+seed*17.;vec3 at=vec3(q.x,p.y-h*.5,q.y);
  b=box(at,vec3(2.8,h*.5,2.8));
  b=min(b,box(vec3(q.x,p.y-h-2.,q.y),vec3(1.3,2.,1.3)));
  b=min(b,box(vec3(q.x,p.y-4.,q.y),vec3(3.2,1.,3.2)));
 }else if(uVariant==2){
  // Industrial canyon with angular buttresses and suspended transverse spans.
  h=26.+seed*17.;vec3 a=vec3(q.x,p.y-h*.5,q.y);
  b=box(a,vec3(2.65,h*.5,2.8));
  vec3 butt=vec3(abs(q.x)-2.3,p.y-7.,q.y);butt.xy=rot(.3)*butt.xy;
  b=min(b,box(butt,vec3(.38,8.,2.9)));
 }else{
  // The street disappears. Entire districts detach into floating islands.
  float lift=8.+seed*5.+(k*.3+uAction*.7)*(8.+seed*7.);
  h=3.+seed*9.;b=box(vec3(q.x,p.y-lift-h*.5,q.y),vec3(2.45,h*.5,2.45));
  float island=box(vec3(q.x,p.y-lift+.7,q.y),vec3(3.2,.7,3.2));b=min(b,island);
 }
 if(lane<.5)b=100.;
 if(uVariant==2){
  // Reserve a broad cross-street/plaza for the sideways camera, including its sightline.
  // Subtracting an extruded rectangle keeps a valid distance to the plaza boundary.
  float plaza=max(abs(p.x)-34.,abs(p.z-12.)-20.);b=max(b,-plaza);
 }
 vec2 hit=vec2(b,material);
 if(uVariant==1){
  // One large stepped landmark anchors the aerial shot above the old empty central lane.
  vec3 hero=p-vec3(0,0,28.);
  float tower=box(hero-vec3(0,9.,0),vec3(4.5,9.,4.5));
  tower=min(tower,box(hero-vec3(0,22.,0),vec3(2.5,4.,2.5)));
  tower=min(tower,box(hero-vec3(0,29.,0),vec3(.9,3.,.9)));
  tower=min(tower,box(hero-vec3(0,34.,0),vec3(.12,2.,.12)));
  float wings=box(vec3(abs(hero.x)-(5.+uAction*3.),hero.y-12.,hero.z),vec3(1.4,.5,4.));
  tower=min(tower,wings);
  // Four flying buttresses and a crown make the landmark legible against plain roofs.
  float buttress=box(vec3(abs(hero.x)-3.7,hero.y-22.,hero.z),vec3(.30,7.,.60));
  tower=min(tower,buttress);
  tower=min(tower,box(vec3(hero.x,hero.y-28.,abs(hero.z)-2.2),vec3(2.8,.25,.3)));
  if(tower<hit.x)hit=vec2(tower,5.);
 }
 if(uVariant==2){
  // A wide factory and twin chimneys dominate the side-on view across the open plaza.
  vec3 hero=p-vec3(0,0,27.);
  float factory=box(hero-vec3(0,9.,0),vec3(10.,9.,3.));
  factory=min(factory,box(hero-vec3(0,21.,0),vec3(2.4,3.,2.4)));
  float chimney=max(length(vec2(abs(hero.x)-6.,hero.z))-1.65,abs(hero.y-27.)-10.);
  factory=min(factory,chimney);if(factory<hit.x)hit=vec2(factory,2.);
 }
 if(uVariant<3){
  float ground=p.y+.15;if(ground<hit.x)hit=vec2(ground,1.);
  float rail=length(vec2(abs(p.x)-3.2,p.y-.14))-.065;
  if(rail<hit.x)hit=vec2(rail,3.);
 }
 float z=mod(p.z+12.,24.)-12.;
 if(uVariant==1||uVariant==2){
  float bridge=box(vec3(p.x,p.y-(uVariant==1?15.:9.),z),vec3(11.,.35,1.1));
  if(bridge<hit.x)hit=vec2(bridge,4.);
 }
 if(uVariant>=3){
  vec3 r=vec3(p.x,p.y-15.-k*8.,z);r.xy=rot(k*.6)*r.xy;
  float halo=length(vec2(length(r.xy)-6.8,r.z))-.16;
  if(halo<hit.x)hit=vec2(halo,3.);
 }
 return hit;
}
vec3 normalAt(vec3 p){vec2 e=vec2(.007,0);return normalize(vec3(city(p+e.xyy).x-city(p-e.xyy).x,city(p+e.yxy).x-city(p-e.yxy).x,city(p+e.yyx).x-city(p-e.yyx).x));}
void main(){
 vec2 p=(vUV*2.-1.)*vec2(uResolution.x/uResolution.y,1.);float k=clamp(uChapterProgress,0.,1.);
 vec3 ro,target;float lens=1.35;
 if(uVariant==0){
  float travel=uChapterTime*24.;
  ro=vec3(sin(k*PI)*1.2,1.5+smoothstep(.35,.95,k)*8.,travel);
  target=ro+vec3(-sin(k*PI)*.6,1.2,22.);
 }else if(uVariant==1){
  // Every roof (including the 36-unit needle) stays below this uninterrupted aerial pass.
  ro=vec3(-24.+k*34.,39.+k*4.,-13.+k*26.);
  target=vec3(0,22.,28.);lens=1.48;
 }else if(uVariant==2){
  // Sideways motion on z=4 inside the cleared plaza, with a fixed factory focal point.
  ro=vec3(-24.+k*48.,6.2+k*1.4,4.);
  target=vec3(0,15.+uAction*2.,27.);lens=1.25;
 }else{
  float travel=uChapterTime*29.;ro=vec3(sin(k*PI)*1.2,16.+k*12.,travel);
  target=ro+vec3(-sin(k*PI)*.6,-1.5,22.);
 }
 cinematicRig(ro,target,p,lens,uVariant==1?.85:(uVariant==3?.3:(uVariant==2?.2:0.)),uVariant==0?.9:.6);
 vec3 rd=cameraBasis(ro,target)*normalize(vec3(p,lens));
 vec3 sky=mix(vec3(.005,.009,.025),uVariant==1?vec3(.14,.085,.045):vec3(.07,.015,.11),exp(-abs(rd.y)*3.));
 if(uVariant==3)sky+=vec3(.02,.07,.1)*max(rd.y+.3,0.);
 sky+=accent(.6)*pow(max(dot(rd,normalize(vec3(.2,.08,1.))),0.),100.)*.25;
 float t=.02;vec2 h;bool hit=false;
 for(int i=0;i<128;i++){h=city(ro+rd*t);if(h.x<.007){hit=true;break;}t+=max(.008,h.x*.72);if(t>170.)break;}
 vec3 c=sky;
 if(hit){
  vec3 at=ro+rd*t,n=normalAt(at);float fres=pow(1.-max(dot(n,-rd),0.),3.);vec3 neon=vec3(.035,.42,.65);
  vec3 sun=normalize(vec3(-1,2,-1));
  float metalSpec=pow(max(dot(reflect(-sun,n),-rd),0.),70.);
  c=vec3(.025,.035,.055)*(.6+max(dot(n,sun),0.));
  // Distance-field ambient occlusion seats towers in their streets.
  float ao=clamp(city(at+n*.6).x/.6*.6+city(at+n*2.).x/2.*.4,.2,1.);c*=ao;
  c+=vec3(.36,.46,.57)*metalSpec*.75;
  if((h.y>1.5&&h.y<2.5)||h.y>4.5){
   float facade=1.-abs(n.y);
   bool landmark=h.y>4.5;
   vec2 windowUV=vec2(abs(n.x)>.5?at.z:at.x,at.y)*vec2(1.1,.75);
   vec2 grid=abs(fract(windowUV)-.5);
   // Windows belong to vertical facades. Roofs remain dark architectural mass.
   float window=(1.-smoothstep(.23,.31,grid.x))*(1.-smoothstep(.24,.32,grid.y))*facade;
   float occupied=step(.60,hash(floor(windowUV)+floor(at.xz/8.)*31.));
   float band=frequencyBand(mod(floor(at.y*.75)+floor(at.z/8.),24.));
   vec3 warm=landmark?vec3(1.2,.65,.18):mix(vec3(.95,.51,.18),vec3(.07,.32,.5),step(.72,hash(floor(at.xz/8.))));
   c*=landmark?vec3(1.6,1.3,.9):vec3(.9,1.,1.1);
   c+=warm*window*occupied*(.23+band*.16+uPulse.y*.30);
   c+=vec3(.025,.09,.14)*fres*facade;
   float scan=pow(.5+.5*cos(at.y*.58-uChapterTime*4.+floor(at.z/8.)),20.);
   c+=neon*window*occupied*scan*(.06+uPulse.x*.22);
   if(landmark){
    vec3 local=at-vec3(0,0,28.);
    float rib=line(abs(local.x)-2.5,.055)+line(abs(local.z)-2.5,.055);
    c+=vec3(.75,.35,.065)*rib*(.35+uPulse.x*.65)*facade;
    c+=vec3(1.,.52,.15)*line(at.y-28.,.08)*.6;
   }
  }else if(h.y>2.5)c=neon*(h.y<3.5?2.:.35)+vec3(.08,.12,.18)*max(n.y,0.);
  else{
   float lane=line(abs(at.x)-.1,.035)*step(.6,fract(at.z*.08));
   float grid=line(min(abs(mod(at.x+.5,1.)-.5),abs(mod(at.z+2.,4.)-2.)),.018);
   float wet=pow(max(dot(reflect(rd,n),normalize(vec3(.1,.08,1.))),0.),18.);
   float stream=pow(.5+.5*cos(at.z*.4-uChapterTime*13.),18.);
   c+=neon*(lane*1.8+grid*.08+exp(-abs(abs(at.x)-3.2)*2.)*(.35+stream*.7+uPulse.x*.55));
   c+=vec3(.2,.3,.45)*wet*.2;
  }
  c=mix(c,sky+vec3(.015,.025,.035),1.-exp(-t*.020));
 }
 // Counterflow aircraft: world-space depth, separate lanes, no screen overlay.
 for(int i=0;i<10;i++){
  float fi=float(i),side=mod(fi,2.)*2.-1.;
  float centreZ=uVariant==0||uVariant>=3?ro.z:24.;
  float tz=centreZ+mod(fi*17.+uChapterTime*(side*23.-9.),150.)-35.;
  vec3 traffic=vec3(side*(2.1+mod(fi,3.)*.45),uVariant>=3?24.:3.4+mod(fi,3.)*2.2,tz);
  float along=dot(traffic-ro,rd),miss=length(ro+rd*along-traffic);
  if(along>0.&&(!hit||along<t))c+=mix(vec3(.04,.6,1.5),vec3(1.7,.25,.035),step(0.,side))*exp(-miss*14.)*(1.+uPulse.z*.6);
 }
 c+=speedDust(p,lens,vec3(.9,.5,1.1)*.6);
 color=vec4(c,1.);
}
