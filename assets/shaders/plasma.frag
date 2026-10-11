#version 330 core
in vec2 vUV;out vec4 color;
uniform vec2 uResolution;uniform float uTime;uniform int uRetro;
#include "common.glsl"
// PLZ.C's nested sinus-field principle, evaluated continuously on the GPU.
float field(vec2 p,float t) {
 float a=sin(p.x*3.4+sin(p.y*2.1+t*.32)*2.4+t*.24);
 float b=sin(p.y*4.+sin(p.x*2.7-t*.23)*2.1-t*.36);
 return a+b;
}
void main() {
 vec2 p=(vUV-.5)*vec2(uResolution.x/uResolution.y,1.)*3.2;
 float t=uTime;
 vec2 warp=vec2(fbm(p*.8+t*.07),fbm(p*.8+8.-t*.05))-.5;
 p+=warp*1.25;
 float f=field(p,t);
 vec3 cyan=vec3(.01,.65,.8),violet=vec3(.27,.025,.48),gold=vec3(1.1,.37,.065);
 vec3 c=mix(violet,cyan,smoothstep(-1.1,1.5,f));
 c=mix(c,gold,pow(.5+.5*sin(f*2.2+t*.15),8.));
 float ridge=pow(.5+.5*sin(f*6.+t*.23),16.);
 c*=.14+.24*ridge;
 c+=mix(cyan,gold,.5+.5*sin(f+t*.1))*ridge*.9;
 vec2 e=vec2(.015,0.);
 vec3 n=normalize(vec3(field(p+e,t)-field(p-e,t),field(p+e.yx,t)-field(p-e.yx,t),.12));
 c+=vec3(.3,.6,.7)*pow(max(dot(reflect(normalize(vec3(-.4,.8,1.)),n),vec3(0,0,1)),0.),18.)*.7;
 float tendril=line(f-.3,.045);
 c+=vec3(.25,.95,1.4)*tendril*.7;
 c*=.65+.35*(1.-smoothstep(.1,1.7,length(p)));
 if(uRetro==1) c=floor(c*12.)/12.;
 color=vec4(c,1.);
}
