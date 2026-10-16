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
 float d=distance*(.83+.16*cos(uProgress*PI));
 float y=.6+.65*sin(time*.17);
 if(uShot==1){d*=.72;y=.2+.35*sin(uProgress*PI);}
 if(uShot==2){d*=.8;y=2.5+.7*sin(uProgress*PI);}
 if(uShot==3){d*=.75;y=.7+1.1*sin(uProgress*PI);}
 return vec3(sin(a)*d,y,cos(a)*d);
}
// Camera-only drive without transients: x smoothed over ~1.5 s, y chapter mean.
uniform vec2 uCamera;
// Motion clock integrated from smoothed speed, for camera-relative streams.
uniform float uFlowTime;
float flightBank(float time){return sin(time*.28)*(.12+uCamera.x*.22)+sin(time*.71)*.05;}
vec2 flightOffset(float time){return vec2(sin(time*.37)*.45,cos(time*.29)*.3);}
vec3 orbitRay(vec2 p,vec3 eye,float time) {
 p=rot(sin(time*.19)*.12*uCamera.x)*p;
 return cameraBasis(eye,vec3(0))*normalize(vec3(p,1.8));
}
vec3 accent(float phase){return mix(vec3(.015,.6,1.5),vec3(1.6,.08,.3),.5+.5*sin(phase+uTheme*1.7));}
// ---- Cinematic camera rig -------------------------------------------------
// Tracked beat grid: x beat phase, y bar phase, z beat-synced shot number in the
// chapter, w seconds since that shot began. uBeatInfo: beat length (s), cut flash.
uniform vec4 uBeat;
uniform vec2 uBeatInfo;
float beatPunch(){return exp(-uBeat.x*uBeatInfo.x*7.);}
// A soft swell on each beat: eases in over ~60 ms and releases over ~0.3 s.
float beatSwell(){float s=uBeat.x*uBeatInfo.x;return smoothstep(0.,.06,s)*exp(-s*3.5);}
float shotHash(float salt){return fract(sin((uBeat.z+1.)*12.9898+salt*78.233+uTheme*3.71+float(uVariant)*1.37)*43758.5453);}
// Each shot (cut on a downbeat every two or four bars in driven chapters) is a
// fresh camera setup around the action: another side, height and distance, then
// a steady glide with a slow push-in. Music reaches the camera only through
// the cut placement, smoothed drive, per-shot rates that stay constant within a
// shot and one soft lens swell per beat. No kick shake, no zoom snaps, and no
// angle that is time multiplied by a fluctuating level (which jumped per kick).
// orbit: 0 keeps the authored path (tunnels, street flights), 1 circles freely
// around the target. roll: bank / barrel-roll amount.
void cinematicRig(inout vec3 eye,inout vec3 target,inout vec2 p,inout float lens,float orbit,float roll){
 if(uRetro==1)return;
 float st=uBeat.w,cutShot=step(.5,uBeat.z);
 float h1=shotHash(1.),h2=shotHash(2.),h3=shotHash(3.);
 float side=h1<.5?-1.:1.;
 vec3 arm=eye-target;
 // 5-13 degrees per second, fixed for the whole shot: an even glide.
 float rate=.08+.05*h3+.1*uCamera.y;
 float swing=side*(cutShot*(.45+.85*h2)+st*rate);
 float lift=cutShot*(h3-.35)*.55;
 arm.xz=rot(swing*orbit)*arm.xz;
 vec3 right=normalize(cross(vec3(0,1,0),arm)+vec3(1e-4,0,0));
 // Rodrigues rotation about the horizontal axis; positive lift raises the eye.
 arm=arm*cos(lift*orbit)-cross(right,arm)*sin(lift*orbit);
 float push=1.-orbit*(.12*(1.-exp(-st*.5))+cutShot*.22*(h3-.5));
 vec3 rigged=target+arm*push;
 // Never drop below the authored height by much: worlds have floors.
 rigged.y=max(rigged.y,min(eye.y,target.y)+.15*(eye.y-target.y));
 eye=mix(eye,rigged,clamp(orbit,0.,1.));
 // A slow bank; tunnel flights in driven chapters roll at one constant rate.
 float bank=roll*(side*(.05+.05*h3)*cutShot+.05*sin(st*.45+h2*6.283)*(.4+uCamera.x));
 bank+=roll*roll*side*st*.28*smoothstep(.75,1.,uCamera.y)*float(orbit<.05);
 p=rot(bank)*p;
 // Handheld float: a slow, smooth drift replaces the kick-driven shake.
 p+=(vec2(noise(vec2(uChapterTime*.55,3.)),noise(vec2(7.,uChapterTime*.47)))-.5)*.007*(.4+uCamera.x);
 lens*=1.-.01*beatSwell()*(.3+uCamera.x);
 // Cuts settle gently instead of crash-zooming.
 lens*=1.+.05*cutShot*exp(-st*3.5);
}
// Camera-space speed dust: depth layers stream past the lens with true parallax
// (near layers sweep outward faster) and stretch radially with velocity.
// Velocity is constant per chapter on the smooth flow clock: dust never jumps.
vec3 speedDust(vec2 p,float lens,vec3 tint){
 if(uRetro==1)return vec3(0);
 vec3 r=normalize(vec3(p,lens));vec3 sum=vec3(0);
 float velocity=.12+uCamera.y*.35;
 for(int i=0;i<4;i++){
  float fi=float(i),phase=fract(fi*.25-uFlowTime*velocity*.25);
  float depth=mix(.7,14.,phase);
  vec2 q=r.xy/r.z*depth+vec2(fi*7.3,fi*3.1);
  vec2 cell=floor(q/1.6),local=fract(q/1.6)-.5;
  float h=hash(cell+fi*17.);
  if(h<.9)continue;
  vec2 c=vec2(hash(cell+3.1),hash(cell+8.7))-.5;vec2 d=local-c*.7;
  vec2 radial=normalize(q+1e-4);float along=dot(d,radial),across=dot(d,vec2(-radial.y,radial.x));
  float stretch=1.+velocity*14./depth;
  float spot=exp(-(along*along/(stretch*stretch)+across*across)*4200.);
  float fade=smoothstep(0.,.25,phase)*smoothstep(1.,.75,phase);
  sum+=tint*spot*fade*(.5+2.*(h-.9))*(.25+uPulse.z*.5+uCamera.x*.35);
 }
 return sum*smoothstep(.25,.6,uCamera.x);
}
