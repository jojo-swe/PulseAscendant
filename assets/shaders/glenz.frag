#version 330 core
in vec3 vNormal;in vec3 vPosition;in vec3 vBary;out vec4 color;
uniform float uTime;uniform int uRetro;uniform int uMirror;uniform float uEnergy;
void main() {
 vec3 n=normalize(vNormal),v=normalize(vec3(0,0,5)-vPosition);
 float facing=abs(dot(n,v)),fresnel=pow(1.-facing,3.);
 vec3 l=normalize(vec3(-2,3,4));
 float diffuse=abs(dot(n,l));
 float spec=pow(abs(dot(reflect(-l,n),v)),42.);
 vec3 iridescent=.5+.5*cos(vec3(0,2,4)+facing*5.+vPosition.y*.8+uTime*.1);
 vec3 base=mix(vec3(.015,.15,.24),iridescent*.4,.7);
 vec3 c=base*(.35+.8*diffuse)+vec3(.8,1.,1.2)*spec*1.5+vec3(.02,.55,.8)*fresnel;
 vec3 d=fwidth(vBary);
 vec3 edge=smoothstep(vec3(0),d*1.35,vBary);
 float wire=1.-min(min(edge.x,edge.y),edge.z);
 c+=mix(vec3(.05,1.,1.7),vec3(1.5,.17,.4),.5+.5*sin(vPosition.y*2.+uTime*.4))*wire*(1.+uEnergy);
 if(uRetro==1) c=floor(c*6.)/6.;
 color=vec4(c,.48+wire*.45+fresnel*.2);
 if(uMirror==1)color*=vec4(.42,.5,.55,.33);
}
