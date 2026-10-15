#version 330 core
in vec2 vUV;out vec4 color;
uniform sampler2D uImage;uniform vec2 uDirection;uniform int uExtract;
vec3 sampleLight(vec2 uv) {
 vec3 c=texture(uImage,uv).rgb;
 // 1: bloom threshold; 2: only hot highlights feed the anamorphic streaks.
 return uExtract==1?max(c-vec3(.5),vec3(0)):(uExtract==2?max(c-vec3(1.1),vec3(0)):c);
}
void main() {
 vec3 c=sampleLight(vUV)*.227027;
 c+=sampleLight(vUV+uDirection*1.384615)*.316216;
 c+=sampleLight(vUV-uDirection*1.384615)*.316216;
 c+=sampleLight(vUV+uDirection*3.230769)*.070270;
 c+=sampleLight(vUV-uDirection*3.230769)*.070270;
 color=vec4(c,1.);
}
