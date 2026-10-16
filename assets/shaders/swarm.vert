#version 330 core
uniform vec2 uResolution;uniform float uTime;
uniform int uScene;
out vec3 vColor;
uniform int uRetro;
#include "common.glsl"
void main(){
 float id=float(gl_VertexID/2),tail=float(gl_VertexID%2);
 float h=hash(vec2(id,3)),a=id*2.399963+uTime*(.25+uCamera.y*.3)-tail*.018;
 float band=frequencyBand(mod(id,24.));
 float radius=2.5+hash(vec2(id,7))*7.+band*.65;
 float burst=1.+uHit*.08;
 vec3 point=vec3(cos(a)*radius,(h-.5)*5.,sin(a)*radius)*burst;
 point.xy=rot(uTime*.07+h*2.)*point.xy;
 vec3 eye=orbitEye(uTime,uScene==5?7.2:4.9),view=transpose(cameraBasis(eye,vec3(0)))*(point-eye);
 view.xy=rot(-sin(uTime*.19)*.12*uCamera.x)*view.xy;
 vec2 projected=view.xy/max(.1,view.z)*1.8;projected.x*=uResolution.y/uResolution.x;
 gl_Position=vec4(projected,0,1);
 if(view.z<.15)gl_Position=vec4(3,3,3,1);
 vColor=accent(h*6.)*(uScene==5?.14+h*.45:.025+h*.08)*(1.-tail*.8)*(.3+band*1.4);
}
