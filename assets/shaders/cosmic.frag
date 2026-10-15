#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;
#include "common.glsl"
// Single-use astrophysical events: ignition, collision, blast, filaments, warp.
vec2 field(vec3 p){
 float k=clamp(uChapterProgress,0.,1.),d=0.,hot=0.;
 float flow=uChapterTime*.85;
 // Plasma advection remains continuous; attacks excite the core, never the camera.
 p.xy=rot(flow*.12)*p.xy;
 if(uVariant==0){
  float r=length(p),a=atan(p.y,p.x);
  float core=exp(-abs(r-(.42+smoothstep(.02,.45,k)*.45+uPulse.x*.085))*18.);
  float ring=exp(-length(vec2(length(p.xy)-(1.4+k*2.7),p.z*2.))*10.);
  float spokes=pow(.5+.5*cos(a*6.-flow*2.),45.)*exp(-abs(p.z)*7.)*exp(-abs(r-(1.+k*2.))*2.);
  d=core+ring*.65+spokes*(1.-smoothstep(.35,.8,k))*.7;
  hot=core+.3*ring;
 }else if(uVariant==1){
  // Two solid stars and an asymmetric tidal bridge. Sparse ejecta preserve dark space.
  float separation=2.5*(1.-clamp(k*.15+uAction*.7,0.,1.))+.28;
  vec3 q=p-vec3(separation,.42,.5),q2=p+vec3(separation,.42,.5);
  float r1=length(q),r2=length(q2),radius=.86+uPulse.x*.055;
  float suns=exp(-pow(r1/radius,4.))+exp(-pow(r2/(radius*.83),4.));
  float corona=exp(-r1*1.6)*.15+exp(-r2*1.6)*.15;
  float bridge=exp(-length(vec2(p.y-.27*sin(p.x*1.7+flow),p.z-.25*p.x))*5.)*exp(-abs(p.x)*.8)*(.2+uAction*.6);
  vec3 eject=p;eject.yz=rot(.55)*eject.yz;
  float streak=pow(.5+.5*sin(atan(eject.z,eject.x)*7.+eject.y*3.+flow),8.);
  float nova=exp(-abs(length(eject*vec3(.72,1.25,1.))-(1.+k*2.))*7.)*streak*uAction*.20;
  // Twisting stellar streamers span the frame while leaving clear dark gaps.
  float stream=length(vec2(p.y-.55*sin(p.x*1.5-flow*1.4),p.z-.48*cos(p.x*1.5-flow*1.4)));
  float extent=exp(-max(abs(p.x)-4.3,0.)*3.);
  float filament=exp(-stream*13.)*extent;
  float sheath=exp(-stream*3.2)*extent;
  float knots=.4+.6*pow(.5+.5*cos(p.x*4.5-flow*5.),8.);
  d=suns*2.4+corona+bridge*.65+nova*2.5+filament*(2.8+uPulse.y*2.)+sheath*.3*knots;
  hot=suns*1.5+bridge+filament*.45;
 }else if(uVariant==2){
  // A tilted, broken remnant with bipolar jets; never a uniform concentric disc.
  vec3 q=p;q.yz=rot(.6)*q.yz;
  float r=length(q*vec3(1.,.72,1.3));
  float shell=exp(-abs(r-(.8+k*2.6+uAction*1.1))*7.);
  float wisps=pow(.5+.5*sin(q.x*3.7+sin(q.z*2.3+flow)+q.y*2.5),4.);
  float cavity=pow(.5+.5*sin(atan(q.z,q.x)*5.+q.y*2.-flow*.8),3.);
  float jets=exp(-length(q.xz)*8.)*exp(-abs(q.y)*.5)*(.4+k);
  // A surviving pulsar anchors the expanding debris; twin braided jets cut diagonally.
  float core=exp(-pow(length(q)/(.39+uPulse.x*.05),4.))*4.;
  vec2 path=vec2(sin(q.y*2.4-flow*2.5),cos(q.y*2.4-flow*2.5))*(.12+abs(q.y)*.09);
  float jetCore=exp(-length(q.xz-path)*15.)*exp(-abs(q.y)*.18);
  float jetSheath=exp(-length(q.xz-path)*3.)*exp(-abs(q.y)*.30);
  float clumps=.55+.45*pow(.5+.5*cos(q.y*5.-flow*7.),6.);
  d=shell*wisps*cavity*1.9+jets*.7+core+jetCore*3.2+jetSheath*.32*clumps;
  hot=core+jetCore*.65+shell*wisps*.5;
 }else if(uVariant==3){
  vec3 q=p;q.xz=rot(k*.5)*q.xz;
  float r=length(q.xz),a=atan(q.z,q.x);
  float spiral=abs(sin(a*2.-r*2.6+flow*1.6));
  float arm=exp(-spiral*18.)*exp(-abs(q.y-sin(r*1.4+flow*1.3)*.23)*10.)*exp(-abs(r-2.1)*.7);
  float stream=exp(-length(vec2(q.x*.8,q.y-1.4*sin(q.z*.8+flow*1.7)))*7.);
  d=arm*.7+stream*.6;hot=stream;
 }else{
  // Longitudinal coils read as an actual departure rather than another orbit.
  float radius=length(p.xy),a=atan(p.y,p.x);
  float z=p.z+uChapterTime*(5.+k*10.);
  float coil=length(vec2(radius-(1.7+.2*sin(z*.7)),sin(z*2.-a)*.65));
  d=exp(-coil*13.)*.8;
  d+=pow(.5+.5*cos(a*9.+z*.3),32.)*exp(-abs(radius-2.4)*12.)*.5;
  hot=exp(-coil*22.);
 }
 // Fine moving filaments add scale inside the existing large shapes.
 float filament=.68+.32*pow(.5+.5*sin(p.x*15.+p.y*11.+p.z*13.-flow*5.),3.);
 return vec2(d*filament,hot);
}
void main(){
 vec2 p=(vUV*2.-1.)*vec2(uResolution.x/uResolution.y,1.);float k=clamp(uChapterProgress,0.,1.);
 vec3 ro,target=vec3(0);
 if(uVariant==0)ro=vec3(.3-k*.6,.2,6.6-k*1.8);
 else if(uVariant==1){ro=vec3(-1.5+k*2.5,1.8,8.3);target=vec3(.15,.1,0.);}
 else if(uVariant==2){ro=vec3(5.3-k*1.2,2.2,4.8);p=rot(-.22)*p;}
 else if(uVariant==3)ro=vec3(.8,5.8-k*1.3,5.7);
 else{ro=vec3(sin(k*PI)*.23,cos(k*PI)*.23,5.);target=ro+vec3(0,0,-12.);p=rot(k*.35)*p;}
 float lens=uVariant==4?1.15:1.65;
 cinematicRig(ro,target,p,lens,uVariant==4?0.:1.,uVariant==4?1.:.6);
 vec3 rd=cameraBasis(ro,target)*normalize(vec3(p,lens));
 vec3 c=vec3(.001,.003,.011);float transmission=1.;
 int count=uRetro==1?48:96;
 float jitter=hash(gl_FragCoord.xy)-.5;
 for(int i=0;i<96;i++){
  if(i>=count)break;float ds=14.4/float(count),t=(float(i)+.5+jitter*.85)*ds;
  vec3 at=ro+rd*t;vec2 d=field(at);
  float band=frequencyBand((atan(at.z,at.x)/PI+1.)*11.99);
  vec3 cool=vec3(.035,.42,1.1),warm=vec3(1.55,.55,.12);
  // The opening uses white-blue energy; collision and blast reveal hot stellar material.
  vec3 fire=mix(cool,warm,clamp(d.y*.8,0.,1.));
  if(uVariant==1)fire=mix(vec3(.035,.40,1.05),vec3(1.8,1.05,.36),clamp(d.y,0.,1.));
  float opacity=clamp(d.x*ds*.6,0.,.7);
  c+=fire*opacity*transmission*(.95+band*.65+uBass*.25+uEvent*.35+uPulse.x*1.45+uPulse.y*d.y*.65);
  transmission*=1.-opacity*.85;
 }
 vec2 q=rd.xy/max(.18,abs(rd.z));
 float clouds=fbm(q*2.+vec2(uChapterTime*.015,0.))*fbm(q*5.);
 c+=mix(vec3(.035,.08,.17),vec3(.18,.07,.035),clouds)*clouds*.16*transmission;
 // Two parallax star layers keep the void alive without covering the hero plasma.
 for(int layer=0;layer<2;layer++){
  float fl=float(layer),scale=110.+fl*95.;
  vec2 stars=q*scale+vec2(uChapterTime*(1.+fl*2.),uChapterTime*.3);
  vec2 cell=floor(stars),f=fract(stars)-.5;float h=hash(cell+fl*19.);
  float point=exp(-dot(f,f)*180.)*step(.983,h);
  c+=vec3(.32,.55,.9)*point*(.6+uPulse.z*.9)*transmission;
 }
 // Hot ejecta follow actual 3D tracks around the binary/remnant, with parallax.
 if(uVariant==1||uVariant==2){
  for(int i=0;i<18;i++){
   float fi=float(i),phase=fi*2.39996+uChapterTime*.48;
   float r=1.6+mod(fi*.73+uChapterTime*.28,3.0);
   vec3 spark=vec3(cos(phase)*r,sin(phase)*r*.65,sin(phase*1.7)*r*.6);
   float along=dot(spark-ro,rd),miss=length(ro+rd*along-spark);
   if(along>0.)c+=mix(vec3(.12,.65,1.2),vec3(1.1,.45,.10),mod(fi,2.))*exp(-miss*36.)*(.45+uPulse.z*.7);
  }
 }
 if(uVariant==0){
  // An expanding full-frame halo is the opening action, with a visible start and finish.
  float wave=length(p)-(.2+k*2.3);c+=vec3(.14,.5,1.)*line(wave,.025)*sin(k*PI)*.45;
 }
 if(uVariant==4)c+=speedDust(p,lens,vec3(.6,.75,1.3));
 color=vec4(c,1.);
}
