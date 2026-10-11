#version 330 core
in vec2 vUV; out vec4 color;
uniform vec2 uResolution; uniform float uTime; uniform int uScene; uniform int uRetro;
#include "common.glsl"
void main() {
 vec2 p=(vUV-.5)*vec2(uResolution.x/uResolution.y,1.);
 vec3 c=vec3(.002,.006,.018);
 if(uScene==0) {
  vec2 q=p-vec2(sin(uTime*.17)*.06,cos(uTime*.13)*.05);
  float r=length(q),a=atan(q.y,q.x),z=1./max(.045,r);
  float spiral=a*7.+z*.65-uTime*1.1;
  float bands=pow(.5+.5*sin(spiral),22.);
  float rings=pow(.5+.5*sin(z*4.-uTime*6.),40.);
  c+=vec3(.015,.055,.09)*exp(-r*2.);
  c+=vec3(.03,.34,.6)*bands*.2*smoothstep(.035,.1,r);
  c+=mix(vec3(.05,.7,1.3),vec3(1.1,.08,.35),.5+.5*sin(a+uTime*.3))*rings*.2;
  c+=vec3(.02,.38,.75)*exp(-r*18.);
 } else {
  float halo=abs(length((p-vec2(0,.07))*vec2(1.,1.05))-.31);
  c+=mix(vec3(.65,.025,.27),vec3(.02,.3,.6),smoothstep(-.2,.3,p.y))*line(halo,.012)*2.;
  c+=vec3(.12,.015,.09)*exp(-length(p)*3.);
  float y=p.y+.22;
  if(y<0.) {
   vec2 g=vec2(p.x,-.35)/max(.008,-y);
   g.y+=uTime*.25;
   vec2 grid=abs(fract(g*1.5)-.5);
   float stripe=1.-smoothstep(.0,.016,min(grid.x,grid.y));
   c+=vec3(.03,.2,.26)*stripe*exp(y*3.)*.6;
   c+=vec3(.3,.03,.08)*exp(-abs(p.x)*8.)*exp(-abs(y)*6.);
  }
 }
 // Stable, subpixel twinkling starfield; suppressed behind the foreground.
 vec2 cell=floor(vUV*uResolution/3.);
 float h=hash(cell);
 float star=step(.997,h)*pow(hash(cell+17.),4.)*(.5+.5*sin(uTime*.6+h*200.));
 c+=vec3(.6,.8,1.)*star;
 color=vec4(c,1.);
}
