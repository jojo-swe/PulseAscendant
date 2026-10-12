#version 330 core
in vec2 vUV;out vec4 color;
uniform float uTime,uEnergy;uniform vec2 uResolution;uniform int uScene,uRetro;
#include "common.glsl"
// Directed equivalents of the native RT sets. Heavy shatter scenes use fewer
// pieces here, preserving the event and silhouette on OpenGL 3.3 hardware.
float eased(float x){x=clamp(x,0.,1.);return x*x*(3.-2.*x);}
float box(vec3 p,vec3 b){vec3 q=abs(p)-b;return length(max(q,0.))+min(max(q.x,max(q.y,q.z)),0.);}
float octa(vec3 p,vec3 b){return (dot(abs(p)/b,vec3(1.))-1.)*min(b.x,min(b.y,b.z))*.57735;}
vec3 unrotate(vec3 p,float yaw,float roll){p.xz=rot(-yaw)*p.xz;p.xy=rot(roll)*p.xy;return p;}
float bar(vec3 p,vec3 a,vec3 b,float r){vec3 pa=p-a,ba=b-a;return length(pa-ba*clamp(dot(pa,ba)/max(.00001,dot(ba,ba)),0.,1.))-r;}
void take(inout vec2 h,float d,float id){if(d<h.x)h=vec2(d,id);}
// A 3x3 neighbourhood contains every nearby surface, but an omitted taller
// column can still be the closest object while marching through empty space.
// Bound the step by its nearest possible x/z footprint. This bound stays well
// above the hit epsilon and therefore never introduces a visible cell wall.
float unseenGridDistance(vec2 pos,vec2 cell,vec2 gridMin,vec2 gridMax,float spacing,float radius){
 float d=200.;
 if(cell.x-2.>=gridMin.x)d=min(d,pos.x-((cell.x-2.)*spacing+radius));
 if(cell.x+2.<=gridMax.x)d=min(d,((cell.x+2.)*spacing-radius)-pos.x);
 if(cell.y-2.>=gridMin.y)d=min(d,pos.y-((cell.y-2.)*spacing+radius));
 if(cell.y+2.<=gridMax.y)d=min(d,((cell.y+2.)*spacing-radius)-pos.y);
 return d;
}
vec2 surroundings(vec3 pos){
 int world=uScene-8,v=uVariant;vec2 h=vec2(200.,7.);
 if(world==0){
  if(v==0){
   for(int j=0;j<9;j++){float jf=float(j),x=(jf-4.)*5.5,height=8.+mod(jf,3.)*3.;take(h,box(unrotate(pos-vec3(x,height-1.,28.+mod(jf,2.)*4.),.1*(jf-4.),-.12*(jf-4.)),vec3(1.25,height,2)),7.);take(h,bar(pos,vec3(x-1.,-.8,26),vec3(x-1.,height*2.-1.,26),.045),3.);}
   float j=clamp(floor(pos.z/3.+4.),0.,7.),z=(j-3.5)*3.;for(int side=-1;side<=1;side+=2){float x=float(side)*6.5;take(h,box(pos-vec3(x,-.65,z),vec3(.8,.45,.9)),7.);take(h,box(pos-vec3(x,-.18,z),vec3(.55,.025,.55)),3.);}
  }else if(v==2){
   for(int j=0;j<3;j++){float z=24.+float(j)*8.;take(h,box(pos-vec3(0,13,z),vec3(25,.7,2)),7.);take(h,bar(pos,vec3(-25,12,z-2.1),vec3(25,12,z-2.1),.08),6.);
    for(int side=-1;side<=1;side+=2){float s=float(side);take(h,box(unrotate(pos-vec3(s*21.,5,z),0.,s*-.16),vec3(1.3,6,2)),1.);take(h,bar(pos,vec3(s*16.,-1,z),vec3(s*23.,12,z),.2),1.);}
   }
   float j=clamp(floor(pos.z/4.+3.),0.,5.);for(int side=-1;side<=1;side+=2)take(h,box(pos-vec3(float(side)*8.,-.65,(j-2.5)*4.),vec3(1.1,.45,1.5)),7.);
  }else if(v==3){
   for(int side=-1;side<=1;side+=2)for(int level=0;level<4;level++){float s=float(side),l=float(level),x=s*(39.-l*2.);take(h,box(unrotate(pos-vec3(x,6.+l*9.,42.+l*3.),s*.18,0.),vec3(14.-l*2.,5,10.-l)),7.);take(h,bar(pos,vec3(x-14.+l*2.,11.+l*9.,31.+l*4.),vec3(x+14.-l*2.,11.+l*9.,31.+l*4.),.07),6.);}
   for(int level=0;level<3;level++){float l=float(level);take(h,box(pos-vec3(0,-.9+l*.15,0),vec3(14.-l*.5,.15,11.-l*.5)),7.);}
  }else if(v==4){
   for(int side=-1;side<=1;side+=2)for(int j=0;j<5;j++){float s=float(side),jf=float(j),z=24.+jf*3.,x=s*(15.+jf*.7),height=11.+jf*.7;take(h,box(unrotate(pos-vec3(x,height-1.,z),s*.12,s*.18),vec3(1.1,height,2.2)),7.);float level=clamp(floor(pos.y/4.+.5),0.,4.);take(h,box(pos-vec3(x-s*1.15,level*4.,z-2.3),vec3(.08,.08,2.2)),6.);}
   take(h,box(pos-vec3(0,-.65,0),vec3(12,.45,19)),7.);
  }else if(v==5){
   float j=clamp(floor((pos.z+24.)/6.+.5),0.,9.),z=-24.+j*6.;
   for(int side=-1;side<=1;side+=2){float s=float(side);take(h,octa(unrotate(pos-vec3(s*22.,16,49),s*.3,s*.15),vec3(7,20,9)),7.);take(h,box(pos-vec3(s*12.,-.4,z),vec3(1.6,.7,1.8)),7.);take(h,box(pos-vec3(s*12.,.33,z),vec3(1.3,.04,.9)),3.);}
  }
 }else if(world==2){
  if(v==0){
   for(int j=0;j<4;j++){float z=22.+float(j)*7.;for(int side=-1;side<=1;side+=2){float s=float(side);take(h,bar(pos,vec3(s*19.,-1,z),vec3(s*19.,12,z),.5),7.);take(h,bar(pos,vec3(s*19.,12,z),vec3(0,20,z),.4),1.);}take(h,bar(pos,vec3(-19,11,z),vec3(19,11,z),.15),6.);}
   take(h,box(pos-vec3(0,6,46),vec3(23,7,1)),7.);
   float j=clamp(floor(pos.z/4.+.5),0.,5.),z=j*4.,vent=clamp(floor((pos.z-z)/.5+.5),-1.,1.);for(int side=-1;side<=1;side+=2){float x=float(side)*12.;take(h,box(pos-vec3(x,-.1,z),vec3(1.5,.9,1.4)),7.);take(h,box(pos-vec3(x,.85,z+vent*.5),vec3(1.2,.04,.1)),1.);}
  }else if(v==1){
   for(int side=-1;side<=1;side+=2){float s=float(side);for(int level=0;level<3;level++){float l=float(level);take(h,box(pos-vec3(s*(18.+l*2.),-.5+l*.55,0),vec3(.8,.6,15)),7.);}
    for(int j=0;j<4;j++){float jf=float(j),x=s*(17.+jf*3.),height=9.+jf*2.,z=27.+jf*2.;take(h,box(pos-vec3(x,height-1.,z),vec3(1.1,height,2)),7.);take(h,box(pos-vec3(x,height*2.-.8,z),vec3(1.3,.12,2.2)),2.);take(h,bar(pos,vec3(x,-.8,z-3.),vec3(x,height*2.-1.,z-3.),.055),4.);}
   }
  }else if(v==2){
   for(int side=-1;side<=1;side+=2){float s=float(side);for(int j=0;j<5;j++){float jf=float(j),x=s*(13.+jf*1.3),height=8.+jf*1.5;take(h,box(unrotate(pos-vec3(x,height-1.,18.+jf*1.8),s*.14,s*.08),vec3(.45,height,3.5)),7.);take(h,bar(pos,vec3(x,height*2.-1.,14.+jf*1.8),vec3(s*8.,height*2.-1.,14.+jf*1.8),.1),6.);}take(h,box(pos-vec3(s*12.,-.4,0),vec3(1.2,.7,7)),7.);}
   take(h,box(pos-vec3(0,-.65,0),vec3(7,.45,8)),7.);
  }else if(v==3){
   for(int side=-1;side<=1;side+=2){float s=float(side);vec3 centre=vec3(s*27.,-12,58);take(h,box(unrotate(pos-centre,s*.16,s*.12),vec3(17,1.8,5)),7.);for(int j=0;j<5;j++)take(h,box(unrotate(pos-centre-vec3((float(j)-2.)*5.,2.1,0),s*.16,0.),vec3(1.4,.6,4.6)),1.);take(h,bar(pos,centre+vec3(-14,0,-5.2),centre+vec3(14,0,-5.2),.06),3.);}
  }else{
   for(int side=-1;side<=1;side+=2){float s=float(side);for(int j=0;j<2;j++){float jf=float(j),x=s*(21.+jf*14.),z=30.+jf*18.,height=35.-jf*8.;take(h,box(pos-vec3(x,height*.5-1.,z),vec3(1.6,height*.5,3)),7.);take(h,bar(pos,vec3(x-s*8.,height,z),vec3(x+s*11.,height,z),.6),1.);take(h,bar(pos,vec3(x,height-1.,z-3.1),vec3(x,height-12.,z-3.1),.08),4.);}
    float j=clamp(floor(pos.z/6.+.5),0.,3.);take(h,box(pos-vec3(s*15.,-.6,j*6.),vec3(3,.5,2.3)),7.);
   }
  }
 }else if(world==3){
  if(v==0){
   for(int side=-1;side<=1;side+=2){float s=float(side);take(h,octa(unrotate(pos-vec3(s*18.,11,24),s*.3,s*.12),vec3(7,14,3)),5.);take(h,bar(pos,vec3(s*18.,-1,19),vec3(s*18.,25,24),.12),1.);}
   for(int level=0;level<3;level++){float l=float(level);take(h,box(pos-vec3(0,-.95+l*.16,0),vec3(9.-l*.45,.16,9.-l*.45)),7.);}
  }else if(v==1){
   for(int j=0;j<3;j++){float jf=float(j),z=26.+jf*7.,y=11.+jf*4.;for(int side=-1;side<=1;side+=2){float s=float(side);take(h,box(unrotate(pos-vec3(s*19.,y-4.,z),0.,s*.12),vec3(1.3,7,2)),7.);take(h,bar(pos,vec3(s*19.,y+3.,z),vec3(s*3.,y+3.,z),.3),1.);}}
   float j=clamp(floor(pos.z/2.4+4.5),0.,8.);take(h,box(pos-vec3(0,-.7,(j-4.)*2.4),vec3(3,.4,.7)),7.);
  }else if(v==2){
   for(int level=0;level<3;level++){float l=float(level);take(h,box(pos-vec3(0,-.7+l*.2,7.+l*4.),vec3(15.-l*2.,.2,12.-l*2.)),8.);}
   take(h,box(unrotate(pos-vec3(-16,9,23),-.16,-.18),vec3(3,10,3)),8.);take(h,box(unrotate(pos-vec3(14,12,27),.15,.12),vec3(3.5,13,3)),8.);take(h,bar(pos,vec3(-16,19,23),vec3(14,25,27),.32),1.);
  }else if(v==3){
   for(int side=-1;side<=1;side+=2){float s=float(side);take(h,bar(pos,vec3(s*22.,-1,29),vec3(s*24.,14,29),.4),7.);take(h,bar(pos,vec3(s*24.,14,29),vec3(s*8.,25,29),.4),1.);float j=clamp(floor((pos.z-10.)/6.+.5),0.,3.);take(h,box(unrotate(pos-vec3(s*(16.+j*2.),-.5,10.+j*6.),s*.2,0.),vec3(1.4,.6,2.5)),7.);}
   take(h,bar(pos,vec3(-8,25,29),vec3(8,25,29),.18),6.);
  }else{
   for(int side=-1;side<=1;side+=2){float s=float(side);take(h,box(pos-vec3(s*11.,-.5,10),vec3(1.7,.6,50)),7.);take(h,octa(unrotate(pos-vec3(s*31.,16,94),s*.3,s*.1),vec3(8,20,5)),8.);}
  }
 }
 return h;
}
vec2 map(vec3 pos){
 int world=uScene-8,v=uVariant;float p=uChapterProgress,a=uAction,lt=uChapterTime;
 vec2 h=vec2(world==2&&v==3?200.:pos.y+1.1,0.);
 if(world==0){
  if(v==0){
   float nearest=clamp(floor(pos.z/.72+14.),0.,27.);
   for(int k=-1;k<=1;k++){float j=clamp(nearest+float(k),0.,27.),z=(j-13.5)*.72,g=eased(p*2.4-j*.035),height=.12+g*(2.8+1.4*sin(j*.21));
    vec3 q=unrotate(pos-vec3(sin(j*.3)*1.2,height-1.,z),j*.025,(-.3+.6*g)*sin(j*.27));take(h,box(q,vec3(2.7,height,.16)),1.);
    take(h,bar(pos,vec3(-2.7,height*2.-1.02,z),vec3(2.7,height*2.-1.02,z),.035),3.);
   }
  }else if(v==1){
   float nearest=clamp(floor(pos.z/4.6+6.),0.,11.);
   for(int k=-1;k<=1;k++){float j=clamp(nearest+float(k),0.,11.),z=(j-5.5)*4.6,open=eased(a+j*.025),height=9.+open*5.;
    for(int side=-1;side<=1;side+=2){float s=float(side);vec3 elbow=vec3(s*(7.+open*1.5),height*.62,z),apex=vec3(s*(.25+open),height,z);
     take(h,bar(pos,vec3(s*8.,-1,z),elbow,.23),1.);take(h,bar(pos,elbow,apex,.23),1.);
     take(h,bar(pos,elbow+vec3(s*-.3,0,-.25),apex+vec3(s*-.3,0,-.25),.045),6.);
     take(h,box(unrotate(pos-vec3(s*9.1,3,z),0.,s*-.08),vec3(.18,4,1.6)),1.);
    }
   }
  }else if(v==2){
   float nearest=clamp(floor(pos.z/.52+28.),0.,55.);
   for(int lane=0;lane<4;lane++)for(int k=-1;k<=1;k++){float j=clamp(nearest+float(k),0.,55.),z=(j-27.5)*.52,phase=z*.3+lt*1.1+float(lane)*.7;
    float height=1.3+(1.+a*2.4)*sin(phase),x=(float(lane)-1.5)*2.4+sin(phase*.7)*a*1.5;
    take(h,box(unrotate(pos-vec3(x,height,z),.06*cos(phase),.35*cos(phase)),vec3(.9,.065,.3)),lane%2==0?1.:2.);
    if(mod(j,7.)<.5)take(h,bar(pos,vec3(x-.85,height+.08,z),vec3(x+.85,height+.08,z),.025),3.);
   }
  }else if(v==3){
   vec2 nearest=clamp(floor(pos.xz/2.4+.5),vec2(-4,-3),vec2(4,3));
   for(int dx=-1;dx<=1;dx++)for(int dz=-1;dz<=1;dz++){
    vec2 cell=nearest+vec2(float(dx),float(dz));if(any(lessThan(cell,vec2(-4,-3)))||any(greaterThan(cell,vec2(4,3))))continue;
    float seed=.5+.5*sin(cell.x*13.+cell.y*7.),g=eased(p*2.-.28*seed),height=.15+g*(3.+15.*seed);
    take(h,box(unrotate(pos-vec3(cell.x*2.4,height-1.,cell.y*2.4),cell.x*.1,0.),vec3(.7,height,.7)),2.);take(h,box(pos-vec3(cell.x*2.4,height*2.-.94,cell.y*2.4),vec3(.72,.035,.72)),3.);
   }
   // Yaw does not mix tower height into x/z; sqrt(.7^2+.7^2)<1.
   h.x=min(h.x,unseenGridDistance(pos.xz,nearest,vec2(-4,-3),vec2(4,3),2.4,1.));
  }else if(v==4){
   for(int j=0;j<36;j++){float jf=float(j),spread=eased(a+.015*jf),angle=(-.65+jf*.038)*spread;vec3 base=vec3((jf-17.5)*.35*spread,1.+jf*.07*spread,(jf-17.5)*.9*spread);
    take(h,box(unrotate(pos-base,angle,-.6*spread+jf*.025),vec3(4.5,.08,2.4)),1.);take(h,bar(pos,base+vec3(-4.5,.15,0),base+vec3(4.5,.15,0),.045),j%3==0?6.:3.);
   }
  }else{
   float nearest=clamp(floor(pos.z/1.3+9.),0.,17.);
   for(int k=-2;k<=2;k++)for(int side=-1;side<=1;side+=2){float s=float(side),j=clamp(nearest+float(k),0.,17.),z=(j-8.5)*1.3,x=s*(3.1+a*(6.+j*.08));
    take(h,box(unrotate(pos-vec3(x,6,z),s*a*.55,s*a*.18),vec3(2.8,7,.32)),1.);take(h,box(unrotate(pos-vec3(x-s*2.8,6,z-.36),0.,s*a*.18),vec3(.06,6.9,.04)),6.);
   }
   take(h,bar(pos,vec3(9,-1,-18),vec3(9,-1,35),.08),3.);take(h,bar(pos,vec3(-9,-1,-18),vec3(-9,-1,35),.08),3.);take(h,octa(unrotate(pos-vec3(0,8,36),.3,0.),vec3(2.4,7,2.4)),2.);
  }
 }else if(world==1){
  float nearest=clamp(floor(pos.z/7.+11.),0.,21.);
  for(int side=-1;side<=1;side+=2)for(int k=-1;k<=1;k++){float s=float(side),j=clamp(nearest+float(k),0.,21.),seed=.5+.5*sin(j*7.1+s),z=(j-10.5)*7.,height=4.+seed*(v==3?24.:14.);
   float x=s*(v==2?13.:11.),lean=v==2?s*a*.2:0.;if(v==3)x=s*(16.+mod(j,3.)*4.);if(v==4)x=s*(13.+a*mod(j,4.)*3.);
   take(h,box(unrotate(pos-vec3(x,height-1.,z),v==4?s*a*.12:0.,lean),vec3(2.9,height,2.6)),1.);
   for(int level=0;level<4;level++){float y=height*(.3+float(level)*.43);take(h,box(unrotate(pos-vec3(x-s*2.94,y,z),0.,lean),vec3(.035,.1,2.4)),mod(j+float(level),3.)<.5?4.:3.);}
  }
  float stripeZ=(clamp(floor(pos.z/5.+15.),0.,29.)-14.5)*5.;for(int side=-1;side<=1;side+=2)take(h,box(unrotate(pos-vec3(float(side)*4.8,-1.075,stripeZ),v==2?float(side)*.12:0.,0.),vec3(.055,.015,1.8)),3.);
  if(v==0){
   take(h,bar(pos,vec3(-23,8,14),vec3(23,8,14),.18),1.);float j=clamp(floor((-25.+55.*p-pos.x)/3.7+.5),0.,7.),x=-25.+55.*p-j*3.7;
   take(h,box(pos-vec3(x,8.7,14),vec3(1.45,.4,.8)),1.);take(h,box(pos-vec3(x,8.7,13.17),vec3(1.25,.07,.02)),4.);
  }else if(v==1){
   take(h,bar(pos,vec3(-5.8,.25,-90),vec3(-5.8,.25,90),.1),1.);float j=clamp(floor((-43.+110.*p-pos.z)/4.8+.5),0.,15.),z=-43.+110.*p-j*4.8;
   take(h,box(pos-vec3(-5.8,1.7,z),vec3(1.05,1.3,2.2)),1.);take(h,box(pos-vec3(-4.73,2,z),vec3(.03,.18,1.8)),3.);
   j=clamp(floor((70.-lt*7.-pos.z)/12.+.5),0.,11.);z=70.-j*12.-lt*7.;float x=mod(j,2.)<.5?.1:2.1;
   take(h,box(pos-vec3(x,.2,z),vec3(.65,.25,1.2)),2.);take(h,box(pos-vec3(x,.22,z-1.23),vec3(.5,.04,.025)),4.);
  }else if(v==2){
   float j=clamp(floor(pos.z/5.5+9.),0.,17.),z=(j-8.5)*5.5,y=2.+a*max(0.,5.-abs(j-8.))*.7;
   for(int side=-1;side<=1;side+=2){float s=float(side);take(h,box(unrotate(pos-vec3(s*(4.5+a*3.),y,z),s*.12,s*a*.2),vec3(3.9,.24,2.4)),1.);take(h,bar(pos,vec3(s*1.2,y+.3,z-2.3),vec3(s*8.,y+.3,z+2.3),.05),4.);}
  }else if(v==3){
   for(int level=0;level<3;level++){float l=float(level),j=clamp(floor((pos.z+lt*(7.+l*2.))/5.+12.),0.,23.),z=(j-11.5)*5.-lt*(7.+l*2.),x=-8.+l*8.,y=9.+l*9.;
    take(h,box(pos-vec3(x,y,z),vec3(.35,.15,1.6)),1.);take(h,box(pos-vec3(x,y,z-1.65),vec3(.28,.06,.04)),level%2==0?3.:4.);take(h,bar(pos,vec3(-17,10.+l*9.,10.+l*14.),vec3(17,10.+l*9.,10.+l*14.),.12),1.);
   }
  }else{
   vec2 cell=clamp(floor(vec2(pos.x/4.+5.,(pos.z-5.)/8.+3.5)),vec2(0),vec2(9,6));float j=cell.x+cell.y*10.,x=(cell.x-4.5)*4.,z=(cell.y-3.)*8.+5.,launch=eased(a-mod(j,7.)*.04),y=4.+launch*(20.+mod(j,5.)*4.);
   take(h,octa(unrotate(pos-vec3(x,y,z),0.,launch*.2),vec3(.75,.25,1.3)),1.);if(launch>.01)take(h,bar(pos,vec3(x,y-.4,z),vec3(x,y-.4-launch*4.,z),.055),3.);take(h,octa(unrotate(pos-vec3(0,17,70),.2,0.),vec3(17,22,12)),2.);
  }
 }else if(world==2){
  if(v==0){
   float j=clamp(floor(pos.z/1.3+13.),0.,25.),z=(j-12.5)*1.3;take(h,box(pos-vec3(0,-.45,z),vec3(3,.18,.42)),1.);
   for(int side=-1;side<=1;side+=2)take(h,box(pos-vec3(float(side)*3.3,.1,z),vec3(.09,.05,.55)),4.);
   j=clamp(floor(pos.z/3.+3.),0.,5.);z=(j-2.5)*3.;float close=eased(a+j*.04);
   for(int side=-1;side<=1;side+=2){float s=float(side);take(h,bar(pos,vec3(s*8.,-1,z),vec3(s*8.,7,z),.22),1.);take(h,bar(pos,vec3(s*8.,7,z),vec3(s*(6.-4.*close),3.8,z),.32),2.);
    float k=clamp(floor(pos.z/.7+5.),0.,9.),attach=eased(a+k*.025),x=s*(9.-7.*attach);take(h,box(unrotate(pos-vec3(x,2.+mod(k,3.)*.3,(k-4.5)*.7),s*attach*.4,s*(1.-attach)*.4),vec3(2,.17,.32)),1.);
   }
   take(h,octa(pos-vec3(0,2,0),vec3(1.2,1.1,5)),2.);
  }else if(v==1){
   vec2 nearest=clamp(floor(pos.xz/1.7+.5),vec2(-7),vec2(7));
   for(int dx=-1;dx<=1;dx++)for(int dz=-1;dz<=1;dz++){
    vec2 cell=nearest+vec2(float(dx),float(dz));if(any(lessThan(cell,vec2(-7)))||any(greaterThan(cell,vec2(7))))continue;
    float wave=sin(cell.x*.4+cell.y*.28-lt*1.8),height=4.+wave*(.3+a*3.);
    take(h,box(unrotate(pos-vec3(cell.x*1.7,height,cell.y*1.7),.08*cell.x,a*wave*.25),vec3(.72,.12,.72)),mod(cell.x+cell.y,3.)<.5?2.:1.);if(mod(cell.x+cell.y,3.)<.5)take(h,box(pos-vec3(cell.x*1.7,height+.15,cell.y*1.7),vec3(.55,.025,.55)),3.);
   }
   // All rotated plate corners lie inside radius sqrt(2*.72^2+.12^2)<1.04.
   h.x=min(h.x,unseenGridDistance(pos.xz,nearest,vec2(-7),vec2(7),1.7,1.04));
   for(int x=-1;x<=1;x+=2)for(int z=-1;z<=1;z+=2)take(h,bar(pos,vec3(float(x)*14.,-1,float(z)*14.),vec3(float(x)*14.,12,float(z)*14.),.25),1.);
  }else if(v==2){
   float j=clamp(floor(pos.z/2.4+2.5),0.,4.),z=(j-2.)*2.4,stroke=(.5+.5*sin(lt*2.7+j*.7))*(.5+frequencyBand(int(j)*4)*2.);
   for(int side=-1;side<=1;side+=2){float s=float(side);take(h,bar(pos,vec3(s*10.,3,z),vec3(s*(4.+stroke),3,z),.55),1.);take(h,box(pos-vec3(s*(3.2+a*3.),3,z),vec3(.6,2.6,.8)),2.);for(int zs=-1;zs<=1;zs+=2)take(h,box(unrotate(pos-vec3(s*(1.9+a*3.),3,float(zs)*1.9),s*a*.15,0.),vec3(1.65,3,1.65)),1.);}
   take(h,bar(pos,vec3(0,-.7,0),vec3(0,8.+a*7.,0),.14+uEvent*.18),3.);
  }else if(v==3){
   float accent=max(.01,uAccentTime),duration=uChapterDuration;
   float collide=eased(lt/accent),burst=eased((lt-accent)/max(.25,duration-accent));
   for(int side=-1;side<=1;side+=2)for(int j=0;j<36;j++){float jf=float(j),s=float(side);vec3 formation=vec3(s*(17.-(17.-1.905)*collide)+(mod(jf,6.)-2.5)*.65,3.+(mod(floor(jf/6.),3.)-1.)*.75,(floor(jf/18.)-.5)*.85),scatter=vec3(sin(jf*2.13+s)*25.,7.+cos(jf*1.37)*16.,sin(jf*3.71)*22.);
    take(h,box(unrotate(pos-formation-scatter*burst,burst*jf*.31,burst*jf*.18),vec3(.28,.3,.35)),side<0?1.:2.);
   }
  }else{
   float launch=a*eased((p-.25)/.75),height=3.+launch*36.;take(h,octa(pos-vec3(0,height,0),vec3(2,7,1.8)),2.);
   for(int side=-1;side<=1;side+=2){float s=float(side);take(h,box(unrotate(pos-vec3(s*3.,height-1.,0),s*.5,s*.18),vec3(3.8,.18,2)),1.);take(h,bar(pos,vec3(s*(5.+a*6.),-1,-3),vec3(s*(5.+a*6.),16,-3),.35),1.);take(h,bar(pos,vec3(s*(5.+a*6.),16,-3),vec3(s*(2.+a*8.),16,0),.22),2.);take(h,bar(pos,vec3(s*1.2,height-2.,0),vec3(s*1.2,height-2.-8.4*(.15+launch),0),.35),3.);}
   take(h,box(pos-vec3(0,-.6,0),vec3(8,.5,10)),1.);
  }
 }else{
  if(v==0){
   for(int x=-1;x<=1;x+=2)for(int y=-1;y<=1;y+=2)for(int z=-1;z<=1;z+=2){vec3 q=unrotate(pos-vec3(float(x)*(1.6+a*2.6),4.+float(y)*(1.6+a*.5),float(z)*(1.6+a*2.6)),a*float(x)*.15,a*float(z)*.1);take(h,box(q,vec3(1.55)),5.);}
   take(h,octa(unrotate(pos-vec3(0,4,0),.3,0.),vec3(.65,2.8,.65)),6.);take(h,bar(pos,vec3(-12,4,-4),vec3(12,4,4),.045),3.);
  }else if(v==1){
   for(int j=0;j<9;j++){float jf=float(j),z=(jf-4.)*2.4;take(h,box(unrotate(pos-vec3(0,3,z),(jf-4.)*.13+a*.4,0.),vec3(1.8,3.5,.24)),5.);take(h,bar(pos,vec3(0,3,z),vec3(12.+a*6.,3.+(jf-4.)*(.7+a*.7),z+(jf-4.)*2.),.055),j%3==0?4.:(j%3==1?3.:6.));}take(h,bar(pos,vec3(-18,3,0),vec3(0,3,0),.08),6.);
  }else if(v==2){
   for(int j=0;j<14;j++){float jf=float(j),angle=(jf-6.5)*.085*(.1+a),x=(jf-6.5)*.45;take(h,box(unrotate(pos-vec3(x,3.7+abs(jf-6.5)*a*.28,0),angle,-angle),vec3(.16,3.2,3.5)),5.);take(h,bar(pos,vec3(x,1,0),vec3(x,5.+a*2.,0),.035),6.);}take(h,box(pos-vec3(0,-.8,0),vec3(7,.2,6)),1.);
  }else if(v==3){
   float burst=a*eased((p-.25)/.75);for(int j=0;j<48;j++){float jf=float(j),angle=jf*2.399963,y=1.-2.*(jf+.5)/48.,r=sqrt(max(0.,1.-y*y)),shell=2.5+burst*(10.+mod(jf,7.)*.8);vec3 direction=vec3(cos(angle)*r,y,sin(angle)*r);take(h,octa(unrotate(pos-vec3(0,4,0)-direction*shell,angle+burst*jf*.07,burst*jf*.12),vec3(.45,1.,.4)),5.);}
   if(a<.98)take(h,octa(unrotate(pos-vec3(0,4,0),.3,0.),vec3(2.8*(1.-a)+.05,4.*(1.-a)+.05,2.8*(1.-a)+.05)),5.);
  }else{
   float nearest=clamp(floor(pos.z/4.8+8.),0.,15.);for(int side=-1;side<=1;side+=2)for(int k=-1;k<=1;k++){float s=float(side),j=clamp(nearest+float(k),0.,15.),z=(j-7.5)*4.8,open=eased(a-j*.012),x=s*(5.+open*5.);take(h,box(unrotate(pos-vec3(x,4.8,z),s*(.2+open*.5),s*.22),vec3(2,6,.25)),5.);take(h,bar(pos,vec3(s*4.,-.9,z),vec3(s*(4.+open*4.),10,z),.055),mod(j,2.)<.5?6.:3.);}
   for(int j=0;j<18;j++){float angle=float(j)*PI/17.;take(h,bar(pos,vec3(0,5,76),vec3(cos(angle)*32.,5.+sin(angle)*28.,82),.08),j%2==0?3.:6.);}take(h,octa(unrotate(pos-vec3(0,5,76),.4,0.),vec3(2.2,5,2.2)),2.);
  }
 }
 vec2 backdrop=surroundings(pos);if(backdrop.x<h.x)h=backdrop;
 return h;
}
vec3 normal(vec3 p){vec2 e=vec2(.003,0);return normalize(vec3(map(p+e.xyy).x-map(p-e.xyy).x,map(p+e.yxy).x-map(p-e.yxy).x,map(p+e.yyx).x-map(p-e.yyx).x));}
vec3 sky(vec3 ray){vec3 top=uScene==8?vec3(.12,.23,.4):(uScene==9?vec3(.018,.04,.09):(uScene==10?vec3(.15,.035,.22):vec3(.22,.28,.4)));vec3 c=mix(vec3(.015,.018,.03),top,clamp(ray.y*.5+.5,0.,1.));c+=vec3(1.3,.5,.13)*pow(max(dot(ray,normalize(vec3(-.5,.4,.7))),0.),160.);c+=vec3(.35,.65,1.1)*pow(max(0.,1.-abs(ray.y-.35)*5.),10.)*pow(abs(ray.x),3.);return c;}
vec3 traceScene(vec3 eye,vec3 ray){
 float distance=0.;vec2 hit=vec2(0.);for(int i=0;i<144;i++){hit=map(eye+ray*distance);if(hit.x<.0025||distance>190.)break;distance+=max(.001,hit.x*.66);}
 if(distance>190.||hit.x>.06)return sky(ray);vec3 p=eye+ray*distance,n=normal(p),reflected=reflect(ray,n);float id=hit.y;
 vec3 albedo=id<.5?vec3(.25,.3,.38):(id<1.5?vec3(.65,.73,.85):(id<2.5?vec3(.95,.72,.25):(id<3.5?vec3(.035,.55,.8):(id<4.5?vec3(.8,.035,.22):(id<5.5?vec3(.65,.88,1):vec3(.95,.72,.25))))));
 if(id>6.5)albedo=id<7.5?vec3(.085,.13,.19):vec3(.22,.27,.32);
 float diffuse=max(dot(n,normalize(vec3(1,2,-3))),0.),fresnel=pow(1.-max(0.,dot(n,-ray)),5.);vec3 c=albedo*(.12+diffuse*.45)+sky(reflected)*albedo*(.75+fresnel*.4);c+=vec3(1.2,.65,.3)*pow(max(0.,dot(reflected,normalize(vec3(-.6,.7,-.3)))),48.);
 if(id>.5&&id<1.5&&uScene==9)c*=.4;if(id>2.5&&id<4.5)c=albedo*(1.8+uBass+uEvent*2.);if(id>5.5&&id<6.5)c=albedo*(1.5+uBass*.7+uEvent);if(id>7.5)c=albedo*(.25+diffuse*.8)+sky(reflected)*.1;
 if(id>4.5&&id<5.5){vec3 bent=refract(ray,n,.69);vec3 through=sky(length(bent)>.01?bent:reflected);float checker=.5+.5*sin((p.x+p.z)*1.4+ray.x*12.);c=mix(through*vec3(.65,.9,1.)+albedo*.12,c,.2+fresnel*.7)+vec3(.08,.12,.16)*checker;}
 if(id<.5){vec2 grid=abs(fract(p.xz*.22)-.5);float line=1.-smoothstep(.018,.045,min(grid.x,grid.y));c+=vec3(.015,.12,.19)*line*(.4+midrange());}return mix(sky(ray),c,exp(-distance*(uScene==9?.018:.006)));
}
void main(){
 float p=uChapterProgress,a=uAction;int v=uVariant,world=uScene-8;vec3 eye=vec3(0),target=vec3(0,2,0);
 if(world==0){
  if(v==0){eye=mix(vec3(16,10,-29),vec3(-16,12,-25),p);target=mix(vec3(0,1,0),vec3(0,3,0),p);}else if(v==1){eye=vec3(-1.7+3.4*p,1.8+4.5*p,-24.+32.*p);target=eye+vec3(0,2,13);}else if(v==2){eye=mix(vec3(15,5,-22),vec3(9,9,-9),p);target=vec3(0,2,1);}else if(v==3){eye=mix(vec3(27,12,-38),vec3(34,46,-48),p);target=mix(vec3(0,6,0),vec3(0,16,0),p);}else if(v==4){eye=mix(vec3(-22,11,-28),vec3(-24,16,-38),p);target=vec3(0,2.8,0);}else{eye=vec3(18.*(1.-eased(p*1.6)),10.-5.*p,-34.+42.*p);target=vec3(0,5,6.+30.*p);}
 }else if(world==1){
  if(v==0){eye=vec3(1.4,1.4+.5*p,-25.+28.*p);target=eye+vec3(0,1,22);}else if(v==1){eye=vec3(1.4,2.1+1.5*a,-32.+69.*p);target=eye+vec3(-.3,.3,20);}else if(v==2){eye=mix(vec3(0,34,-28),vec3(0,30,-2),p);target=vec3(0,3.+4.*a,14);}else if(v==3){eye=mix(vec3(0,48,-44),vec3(0,54,-9),p);target=vec3(0,18,15.+20.*p);}else{eye=mix(vec3(0,3,-24),vec3(0,20,-35),p);target=vec3(0,9.+a*15.,18);}
 }else if(world==2){
  if(v==0){eye=mix(vec3(18,9,-22),vec3(13,6,-14),p);target=vec3(0,2,0);}else if(v==1){eye=mix(vec3(24,20,-29),vec3(18,14,-25),p);target=vec3(0,4,0);}else if(v==2){eye=mix(vec3(20,10,-27),vec3(18,13,-24),p);target=vec3(0,4.5,0);}else if(v==3){eye=mix(vec3(8,8,-37),vec3(-5,13,-43),p);target=vec3(0,3,0);}else{float height=3.+a*eased((p-.25)/.75)*36.;eye=vec3(18.+(height-3.)/36.*5.,10.+height*.65,-26);target=vec3(0,height,0);}
 }else{
  if(v==0){eye=mix(vec3(13,7,-19),vec3(9,9,-17),p);target=vec3(0,4,0);}else if(v==1){eye=mix(vec3(-19,12,-25),vec3(-14,10,-20),p);target=vec3(3,3,0);}else if(v==2){eye=mix(vec3(-14,8,-19),vec3(-11,6,-16),p);target=vec3(0,4,0);}else if(v==3){eye=mix(vec3(7,7,-25),vec3(-3,10,-31),p);target=vec3(0,4,0);}else{eye=vec3(0,2.2+2.3*p,-28.+66.*p);target=eye+vec3(0,.8,20);}
 }
 vec2 uv=(vUV-.5)*vec2(uResolution.x/uResolution.y,1.);vec3 ray=cameraBasis(eye,target)*normalize(vec3(uv,1.55));color=vec4(traceScene(eye,ray),1);
}
