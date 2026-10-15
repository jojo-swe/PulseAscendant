#version 330 core
uniform vec2 uResolution;uniform float uTime;
out vec3 vColor;
uniform int uRetro;
#include "common.glsl"
void main(){
 float id=float(gl_VertexID/2),tail=float(gl_VertexID%2),h=hash(vec2(id,1));
 vec3 point=vec3((hash(vec2(id,2))-.5)*32.,(hash(vec2(id,3))-.5)*22.,mod(h*90.-uTime*28.+30000.,90.)+.2);
 float band=frequencyBand(mod(id,24.));
 point.z+=tail*(.15+uDrive*.7+band*1.2);
 point.xy=rot(-flightBank(uTime))*point.xy;
 vec2 p=point.xy/point.z*1.25;p.x*=uResolution.y/uResolution.x;
 gl_Position=vec4(p,0,1);
 float fade=smoothstep(.5,3.,point.z)*(1.-smoothstep(55.,90.,point.z));
 vColor=accent(h*8.)*fade*(.008+h*.035)*(1.-tail*.8)*(.35+band*1.3+treble()*.3);
}
