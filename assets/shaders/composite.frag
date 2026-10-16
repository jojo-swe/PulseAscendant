#version 330 core
in vec2 vUV;out vec4 color;
uniform sampler2D uImage,uBloom,uStreak;uniform int uRetro,uScene;uniform float uFade,uTime,uMatte;
#include "common.glsl"
vec3 antialias(vec2 uv){
 vec2 texel=1./vec2(textureSize(uImage,0));
 vec3 m=texture(uImage,uv).rgb,nw=texture(uImage,uv+texel*vec2(-1,1)).rgb,ne=texture(uImage,uv+texel).rgb;
 vec3 sw=texture(uImage,uv-texel).rgb,se=texture(uImage,uv+texel*vec2(1,-1)).rgb;
 vec3 weights=vec3(.299,.587,.114);
 float lm=dot(m,weights),lnw=dot(nw,weights),lne=dot(ne,weights),lsw=dot(sw,weights),lse=dot(se,weights);
 float low=min(lm,min(min(lnw,lne),min(lsw,lse))),high=max(lm,max(max(lnw,lne),max(lsw,lse)));
 // Contrast-adaptive sharpening restores edge detail lost to a reduced scene scale.
 vec3 sharpened=m+(m*4.-nw-ne-sw-se)*.11*clamp(1.-(high-low)/max(high,.05),0.,1.);
 if(high-low<max(.02,high*.08))return sharpened;
 vec2 direction=vec2(-((lnw+lne)-(lsw+lse)),(lnw+lsw)-(lne+lse));
 float reduce=max((lnw+lne+lsw+lse)*.03125,.0078125);
 direction=clamp(direction/(min(abs(direction.x),abs(direction.y))+reduce),vec2(-6),vec2(6))*texel;
 vec3 a=.5*(texture(uImage,uv+direction*(-1./6.)).rgb+texture(uImage,uv+direction*(1./6.)).rgb);
 vec3 b=a*.5+.25*(texture(uImage,uv-direction*.5).rgb+texture(uImage,uv+direction*.5).rgb);
 float lb=dot(b,weights);return lb<low||lb>high?a:b;
}
void main() {
 vec2 centre=vUV-.5;
 // 2.39:1 scope letterbox cropped from the 16:9 frame.
 float aspect=float(textureSize(uImage,0).x)/float(textureSize(uImage,0).y);
 float half_=.5*aspect/2.39;
 if(uMatte>.5&&uRetro==0&&abs(centre.y)>half_){color=vec4(0,0,0,1);return;}
 vec3 c;
 if(uRetro==0){
  // Radial speed blur: smoothed velocity and cuts smear the frame outward from
  // the vanishing point. Kicks barely touch it: a per-kick smear read as a jolt.
  float cutKick=max(uCut,uBeatInfo.y);
  float speed=uEvent*.004+cutKick*.02+uPulse.x*.0015+uCamera.x*uCamera.x*.006;
  vec3 radial=vec3(0);float r2=dot(centre,centre);
  for(int i=0;i<8;i++){float f=float(i)/7.;radial+=texture(uImage,vUV-centre*speed*f*(.4+r2*2.4)).rgb;}
  radial/=8.;
  c=mix(radial,antialias(vUV),.55-.45*clamp(speed*16.,0.,1.));
  // Lens chromatic aberration grows toward the frame edge and on hits.
  float ca=(.0016+.0012*uPulse.x+.004*cutKick)*r2*4.;
  c.r=mix(c.r,texture(uImage,vUV+centre*ca).r,.7);
  c.b=mix(c.b,texture(uImage,vUV-centre*ca).b,.7);
  vec3 bloom=texture(uBloom,vUV).rgb;
  vec3 streak=texture(uStreak,vUV).rgb;
  c+=bloom*(.42+uPulse.x*.22+uPulse.y*.1+uEvent*.12);
  // Anamorphic streaks carry a cool blue cast, as through a scope lens.
  c+=streak*vec3(.45,.65,1.25)*(.55+uPulse.x*.5);
  // Exposure lifts on beats and kicks; a white flash marks chapter cuts and a
  // faint one beat-synced shot cuts.
  c*=1.+.1*beatPunch()*uDrive+.12*uPulse.x;
  c+=vec3(.9,.95,1.)*(uCut*.3+uBeatInfo.y*.05)*exp(-dot(c,vec3(.33))*.3);
 }else c=texture(uImage,vUV).rgb;
 float vig=pow(16.*vUV.x*vUV.y*(1.-vUV.x)*(1.-vUV.y),.22);
 c*=mix(.32,1.,vig);
 if(uRetro==0){
  float luminance=dot(c,vec3(.2126,.7152,.0722));
  // Blockbuster split tone: teal shadows, warm highlights; deeper blacks.
  vec3 shadows=vec3(.82,1.02,1.12),highlights=vec3(1.12,1.,.86);
  c*=mix(shadows,highlights,smoothstep(.05,1.4,luminance));
  c=max(c-vec3(.006),vec3(0))*1.08;
  c=mix(vec3(luminance),c,1.22);c=max(c,vec3(0));
 }
 // ACES-inspired bounded filmic curve, followed by gamma encoding.
 c=clamp((c*(2.51*c+.03))/(c*(2.43*c+.59)+.14),0.,1.);
 if(uRetro==0){
  // Gentle S-curve after tone mapping adds punch without clipping.
  c=mix(c,c*c*(3.-2.*c),.35);
 }
 c=pow(c,vec3(1./2.2))*uFade;
 if(uRetro==0){
  // Animated film grain, strongest in mid-tones.
  float g=hash(vUV*vec2(1931.,1093.)+fract(uTime*7.31)*vec2(113.,71.))-.5;
  float l=dot(c,vec3(.333));c+=g*.045*(l*(1.-l)*2.+.15);
 }
 if(uRetro==1) c*=.92+.08*sin(vUV.y*200.*3.14159265);
 color=vec4(clamp(c,0.,1.),1.);
}
