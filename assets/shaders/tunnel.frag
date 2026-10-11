#version 330 core
in vec3 vColor; in float vAlpha; out vec4 color;
uniform int uRetro;
void main() {
 float r=length(gl_PointCoord-.5)*2.;
 if(r>1.) discard;
 float a=uRetro==1?1.:exp(-r*r*5.);
 color=vec4(vColor*a*vAlpha,1.);
}
