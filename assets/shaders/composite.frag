#version 330 core
in vec2 vUV;out vec4 color;
uniform sampler2D uImage,uBloom;uniform int uRetro,uScene;uniform float uFade,uTime;
#include "common.glsl"
vec3 antialias(vec2 uv){
 vec2 texel=1./vec2(textureSize(uImage,0));
 vec3 m=texture(uImage,uv).rgb,nw=texture(uImage,uv+texel*vec2(-1,1)).rgb,ne=texture(uImage,uv+texel).rgb;
 vec3 sw=texture(uImage,uv-texel).rgb,se=texture(uImage,uv+texel*vec2(1,-1)).rgb;
 vec3 weights=vec3(.299,.587,.114);
 float lm=dot(m,weights),lnw=dot(nw,weights),lne=dot(ne,weights),lsw=dot(sw,weights),lse=dot(se,weights);
 float low=min(lm,min(min(lnw,lne),min(lsw,lse))),high=max(lm,max(max(lnw,lne),max(lsw,lse)));
 if(high-low<max(.02,high*.08))return m;
 vec2 direction=vec2(-((lnw+lne)-(lsw+lse)),(lnw+lsw)-(lne+lse));
 float reduce=max((lnw+lne+lsw+lse)*.03125,.0078125);
 direction=clamp(direction/(min(abs(direction.x),abs(direction.y))+reduce),vec2(-6),vec2(6))*texel;
 vec3 a=.5*(texture(uImage,uv+direction*(-1./6.)).rgb+texture(uImage,uv+direction*(1./6.)).rgb);
 vec3 b=a*.5+.25*(texture(uImage,uv-direction*.5).rgb+texture(uImage,uv+direction*.5).rgb);
 float lb=dot(b,weights);return lb<low||lb>high?a:b;
}
void main() {
 vec2 centre=vUV-.5;
 float speed=(uEvent*.002+uCut*.006)*float(uRetro==0);
 vec3 c=vec3(0);
 for(int i=0;i<6;i++)c+=texture(uImage,vUV-centre*speed*float(i)).rgb/6.;
 if(uRetro==0)c=mix(c,antialias(vUV),.7);
 if(uRetro==0) {
  float ca=.0007*(uEvent+uHit*.3)*length(centre);
  c.r=mix(c.r,texture(uImage,vUV+centre*ca).r,.35);
  c.b=mix(c.b,texture(uImage,vUV-centre*ca).b,.35);
  c+=texture(uBloom,vUV).rgb*(.4+uBass*.12+uEvent*.12);
  c+=vec3(.02,.035,.05)*uCut;
 }
 float vig=pow(16.*vUV.x*vUV.y*(1.-vUV.x)*(1.-vUV.y),.17);
 c*=mix(.48,1.,vig);
 // ACES-inspired bounded filmic curve, followed by gamma encoding.
 c=clamp((c*(2.51*c+.03))/(c*(2.43*c+.59)+.14),0.,1.);
 c=pow(c,vec3(1./2.2))*uFade;
 if(uRetro==1) c*=.92+.08*sin(vUV.y*200.*3.14159265);
 color=vec4(c,1.);
}
