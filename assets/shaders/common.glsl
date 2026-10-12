const float PI=3.14159265359;
uniform float uDrive,uBass,uHit,uProgress,uCut,uTheme;
uniform int uShot;
uniform int uVariant;
uniform float uChapterProgress,uChapterTime,uEvent,uAction;
uniform float uAccentTime,uChapterDuration;
// Independent 100Hz measured low/mid/high attacks and low-onset phase.
uniform vec4 uPulse;
uniform float uSpectrum[24];
float frequencyBand(float index){return uSpectrum[int(clamp(floor(index),0.,23.))];}
float midrange(){return (uSpectrum[10]+uSpectrum[12]+uSpectrum[14])*.333333;}
float treble(){return (uSpectrum[18]+uSpectrum[20]+uSpectrum[22])*.333333;}
float hash(vec2 p) { return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }
float noise(vec2 p) {
 vec2 i=floor(p),f=fract(p); f=f*f*(3.-2.*f);
 return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+1.),f.x),f.y);
}
float fbm(vec2 p) {
 float f=0.,a=.5; mat2 m=mat2(.8,-.6,.6,.8);
 for(int i=0;i<4;i++) {f+=a*noise(p);p=m*p*2.03+3.7;a*=.5;} return f;
}
mat2 rot(float a) {float c=cos(a),s=sin(a);return mat2(c,-s,s,c);}
vec3 palette(float t) {return .5+.5*cos(6.28318*(vec3(1.,.82,.62)*t+vec3(.0,.18,.4)));}
float line(float d,float width) {return exp(-abs(d)/width);}
mat3 cameraBasis(vec3 eye,vec3 target) {
 vec3 f=normalize(target-eye),r=normalize(cross(f,vec3(0,1,0)));
 return mat3(r,cross(r,f),f);
}
vec3 orbitEye(float time,float distance) {
 float a=time*.22+float(uShot)*1.35;
 float d=distance*(.83+.16*cos(uProgress*PI))-.12*uHit;
 float y=.6+.65*sin(time*.17);
 if(uShot==1){d*=.72;y=.2+.35*sin(uProgress*PI);}
 if(uShot==2){d*=.8;y=2.5+.7*sin(uProgress*PI);}
 if(uShot==3){d*=.75;y=.7+1.1*sin(uProgress*PI);}
 return vec3(sin(a)*d,y,cos(a)*d);
}
float flightBank(float time){return sin(time*.28)*(.12+uDrive*.22)+sin(time*.71)*.05;}
vec2 flightOffset(float time){return vec2(sin(time*.37)*.45,cos(time*.29)*.3);}
vec3 orbitRay(vec2 p,vec3 eye,float time) {
 p=rot(sin(time*.19)*.12*uDrive)*p;
 return cameraBasis(eye,vec3(0))*normalize(vec3(p,1.8-uHit*.08));
}
vec3 accent(float phase){return mix(vec3(.015,.6,1.5),vec3(1.6,.08,.3),.5+.5*sin(phase+uTheme*1.7));}
