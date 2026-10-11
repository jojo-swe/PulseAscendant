#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;
#include "common.glsl"
float box(vec3 p,vec3 b){vec3 q=abs(p)-b;return length(max(q,0.))+min(max(q.x,max(q.y,q.z)),0.);}
vec2 structure(vec3 p) {
 vec3 q=p;q.z=mod(q.z+4.,8.)-4.;
 float radius=3.1+frequencyBand(mod(abs(floor((p.z+4.)/8.)),24.))*.4;
 float arch=length(vec2(length(q.xy)-radius,q.z))-.12;
 vec3 f=q;f.x=abs(f.x)-4.3;
 float scale=1.;
 for(int i=0;i<3;i++){f=abs(f)-vec3(.65,.8,.65)*scale;f.xy=rot(.55+float(i)*.18)*f.xy;scale*=.55;}
 float lattice=box(f,vec3(.13,.7,.14))*.75;
 return lattice<arch?vec2(lattice,1.):vec2(arch,2.);
}
vec3 normalAt(vec3 p){vec2 e=vec2(.003,0);return normalize(vec3(structure(p+e.xyy).x-structure(p-e.xyy).x,structure(p+e.yxy).x-structure(p-e.yxy).x,structure(p+e.yyx).x-structure(p-e.yyx).x));}
void main() {
 vec2 p=(vUV*2.-1.)*vec2(uResolution.x/uResolution.y,1.);
 p=rot(flightBank(uTime)*1.8+float(uShot)*PI*.5)*p;
 vec3 ro=vec3(flightOffset(uTime)*.9,uTime*13.);
 vec3 rd=normalize(vec3(p,1.25));
 float t=.03,glow=0.;vec2 h;bool hit=false;
 for(int i=0;i<112;i++){
  h=structure(ro+rd*t);float stepD=clamp(h.x*.65,.006,.8);
  if(h.y>1.5)glow+=exp(-abs(h.x)*15.)*stepD;
  if(h.x<.003){hit=true;break;}t+=stepD;if(t>80.)break;
 }
 vec3 c=vec3(.003,.006,.018),neon=accent(uTime*.025);
 if(hit){vec3 at=ro+rd*t,n=normalAt(at),l=normalize(ro+vec3(2,3,1)-at);
  float spec=pow(max(dot(reflect(-l,n),-rd),0.),48.);
  float edge=pow(1.-max(dot(n,-rd),0.),3.);
  c=vec3(.08,.12,.17)*(.3+max(dot(n,l),0.))+vec3(.4,.65,.9)*spec;
  c+=neon*(edge*.5+float(h.y>1.5)*(1.2+uBass+uHit));
  c*=exp(-t*.025);
 }
 c+=neon*glow*.08;
 c+=neon*.04*pow(max(rd.z,0.),50.);
 color=vec4(c,1);
}
