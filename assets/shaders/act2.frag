#version 330 core
in vec2 vUV;out vec4 color;
uniform float uTime,uEnergy;uniform vec2 uResolution;uniform int uScene,uRetro;
#include "common.glsl"
float box(vec3 p,vec3 b){vec3 q=abs(p)-b;return length(max(q,0.))+min(max(q.x,max(q.y,q.z)),0.);}
vec2 map(vec3 p){
 float d=p.y+1.2,id=0.;int world=uScene-8;
 if(world==0){vec3 q=p;q.z=mod(q.z+4.5,9.)-4.5;float a=atan(q.y+1.,q.x),r=length(q.xy+vec2(0,1));float arch=max(abs(r-6.5)-.18,abs(q.z)-.4);arch=max(arch,-q.y-1.);if(arch<d){d=arch;id=1.;}float orb=length(vec3(p.x-1.8*sin(uTime*.35),p.y-2,q.z))-1.-uBass*.2;if(orb<d){d=orb;id=2.;}}
 if(world==1){vec3 q=p;q.z=mod(q.z+3.,6.)-3.;q.x=abs(q.x)-10.;float b=box(q-vec3(0,5,0),vec3(2.4,6,2.3));if(b<d){d=b;id=1.;}}
 if(world==2){float a=atan(p.z,p.x),r=length(p.xz);vec3 q=vec3(mod(a+uTime*.15,.19635)*r-.09817*r,p.y-1.4,r-5);float b=box(q,vec3(.2,1.9,.5));if(b<d){d=b;id=2.;}b=length(p-vec3(0,2,0))-1.6-uBass*.1;if(b<d){d=b;id=1.;}}
 if(world==3){vec3 q=p;q.xz=rot(uTime*.17)*q.xz;float b=(abs(q.x)+abs(q.y-2)+abs(q.z)-3.)*.57735;if(b<d){d=b;id=2.;}float a=atan(p.z,p.x),r=length(p.xz);q=vec3(mod(a,.3927)*r-.19635*r,p.y-1.5,r-6);b=box(q,vec3(.2,2,.2));if(b<d){d=b;id=1.;}}
 return vec2(d,id);
}
vec3 normal(vec3 p){vec2 e=vec2(.004,0);return normalize(vec3(map(p+e.xyy).x-map(p-e.xyy).x,map(p+e.yxy).x-map(p-e.yxy).x,map(p+e.yyx).x-map(p-e.yyx).x));}
vec3 sky(vec3 ray){return mix(vec3(.015,.02,.03),vec3(.04,.09,.16),clamp(ray.y*.5+.5,0.,1.))+vec3(1.,.3,.08)*pow(max(dot(ray,normalize(vec3(-.5,.4,.7))),0.),80.);}
vec3 traceScene(vec3 eye,vec3 ray){float t=0.;vec2 h;for(int i=0;i<120;++i){h=map(eye+ray*t);if(h.x<.002||t>180.)break;t+=h.x*.7;}if(t>180.)return sky(ray);vec3 p=eye+ray*t,n=normal(p),r=reflect(ray,n);vec3 albedo=h.y<.5?vec3(.12,.18,.25):(h.y<1.5?vec3(.1,.35,.5):vec3(.7,.3,.08));float light=max(dot(n,normalize(vec3(1,2,3))),0.);vec3 c=albedo*(.15+light*.8)+sky(r)*(.5+.3*uBass);if(h.y<.5){vec2 grid=abs(fract(p.xz*.22)-.5);float line=1.-smoothstep(.018,.05,min(grid.x,grid.y));c+=accent(p.z*.025+uTime*.03)*line*(.6+.4*midrange());c*=.75+.25*sin(p.z*.4-uTime*1.2);}if(uScene==9&&h.y>.5)c+=accent(p.z*.05)*pow(max(0.,sin(p.y*6.)),32.)*(.5+midrange());return mix(sky(ray),c,exp(-t*.012));}
void main(){vec2 p=(vUV-.5)*vec2(uResolution.x/uResolution.y,1.);vec3 eye,target=vec3(0,1.4,0);if(uScene==8||uScene==9){eye=vec3(sin(uTime*.17)*2,1.5+sin(uTime*.11)*.5,mod(uTime*(uScene==9?5.:2.),70.)-35.);target=eye+vec3(.1,.2,5);}else{float a=uTime*.13+float(uShot)*1.57;eye=vec3(sin(a)*11.,3.5+sin(uTime*.2)*2.,cos(a)*11.);}vec3 ray=cameraBasis(eye,target)*normalize(vec3(p,1.55));color=vec4(traceScene(eye,ray),1);}
