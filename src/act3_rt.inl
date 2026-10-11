// Member implementation of HardwareRT::Impl. All objects enter the real TLAS.
void act3Geometry(const TimelineFrame& f){
 const float t=float(f.motionSeconds);const int world=f.scene-12;
 const V3 cyan{.035f,.55f,.8f},gold{.95f,.72f,.25f},pink{.8f,.035f,.22f},steel{.65f,.73f,.85f};
 const auto link=[&](V3 a,V3 b,float radius,V3 material,float metal){V3 d=b-a;float length=std::sqrt(dot(d,d));add(1,(a+b)*.5f,{radius,length*.5f+radius,radius},std::atan2(d.z,-d.x),std::acos(std::clamp(d.y/length,-1.f,1.f)),material,metal,.18f);};
 if(world==0){
  const V3 ceramic{.78f,.63f,.41f},sand{.7f,.35f,.12f};add(4,{0,0,0},{1,1,1},0,0,sand,.04f,.8f);
  // Hero walker and six distant walkers establish scale. Joints and feet move independently.
  for(int walker=0;walker<7;++walker){
   float angle=walker*2*pi/6,r=walker?32.f:0,walk=t*1.9f+walker;V3 base{std::sin(angle)*r,0,std::cos(angle)*r};
   add(1,base+V3{0,2.7f+.12f*std::sin(walk),0},{2.7f,1.15f,1.45f},0,0,ceramic,.3f,.24f);
   for(int rib=0;rib<7;++rib){float x=(rib-3)*.6f;add(3,base+V3{x,2.7f,0},{.85f,1.25f,.75f},pi*.5f,0,steel,.85f,.2f,{},0,pi*.5f);}
   add(2,base+V3{0,4.3f,0},{1.3f,1.4f,1},0,0,ceramic,.2f,.3f);
   add(0,base+V3{0,4.4f,1.05f},{.8f,.06f,.04f},0,0,cyan,0,.2f,cyan*(2+f.hit*5));
   for(int leg=0;leg<4;++leg){
    float side=leg<2?-1.f:1.f,front=leg%2?-1.f:1.f,phase=walk+leg*pi*.5f;
    V3 hip=base+V3{side*1.8f,2,front*.8f},knee=base+V3{side*2.3f,.3f,front*.8f+std::sin(phase)*.4f},foot=base+V3{side*2.5f,-1.5f+std::max(0.f,std::sin(phase))*.35f,front*.8f+std::cos(phase)*.7f};
    add(1,hip,{.35f,.35f,.35f},0,0,steel,.9f,.15f);
    link(hip,knee,.24f,ceramic,.3f);add(1,knee,{.28f,.28f,.28f},0,0,gold,.85f,.18f);
    link(knee,foot,.17f,steel,.8f);
    add(0,foot,{.4f,.16f,.7f},0,0,ceramic,.3f,.25f);
   }
  }
  for(int i=0;i<160;++i){float a=i*2.39996f+t*.15f,r=6+std::fmod(i*1.73f,58.f);add(2,{std::sin(a)*r,1+std::sin(i+t*2)*1.4f,std::cos(a)*r},{.035f,.035f,.15f},a,0,gold,.2f,.4f,gold*(.3f+f.spectrum[i%24]));}
 }else if(world==1){
  const V3 green{.035f,.7f,.28f},bark{.08f,.22f,.19f};add(0,{0,-2.1f,0},{70,.3f,70},0,0,{.025f,.12f,.1f},.65f,.18f);
  for(int tree=0;tree<7;++tree){
   float a=tree*2*pi/6,r=tree?29.f:0;V3 root{std::sin(a)*r,-1.8f,std::cos(a)*r};
   add(0,root+V3{0,4,0},{.35f,4,.35f},0,.05f*std::sin(t+tree),bark,.6f,.25f);
   for(int level=0;level<3;++level)for(int branch=0;branch<7;++branch){
    float angle=branch*2*pi/7+level*.5f+.08f*std::sin(t*.8f+branch),radius=2+level*.7f,y=2+level*1.7f+f.spectrum[(branch+level*7)%24]*.5f;
    V3 tip=root+V3{std::sin(angle)*radius,y+2,std::cos(angle)*radius};
    add(0,root+V3{std::sin(angle)*radius*.5f,y+1,std::cos(angle)*radius*.5f},{.09f,.09f,radius*.55f},angle,0,bark,.5f,.3f);
    add(2,tip,{.6f,.22f,1.1f},angle,.25f*std::sin(t+branch),green,.3f,.13f,green*(.2f+f.spectrum[branch*3%24]*1.5f));
   }
  }
  for(int i=0;i<200;++i){float a=i*2.39996f+t*.3f,r=2+std::fmod(i*1.17f,18.f);V3 e=i%3?green:cyan;add(1,{std::sin(a)*r,1+std::fmod(i*.47f+t*.8f,9.f),std::cos(a)*r},{.055f,.055f,.055f},0,0,e,0,.2f,e*(2+f.hit*4));}
  add(3,{0,7,0},{3,3,3},t*.15f,.25f,cyan,.7f,.15f,cyan*.5f,0,pi*.5f);
 }else if(world==2){
  const V3 stone{.32f,.29f,.42f};add(1,{0,3,0},{2.3f,2.3f,2.3f},0,0,{.01f,.008f,.02f},.98f,.08f);
  for(int ring=0;ring<5;++ring)add(3,{0,3,0},{3.2f+ring*.3f,3.2f+ring*.3f,3},t*.3f+ring,.25f*std::sin(t*.4f+ring),pink,.4f,.15f,pink*(1+f.bass*2),0,pi*.5f+ring*.22f);
  for(int i=0;i<48;++i){float a=i*2.39996f+t*(i%2?.08f:-.07f),r=8+std::fmod(i*1.73f,6.f),y=std::sin(t*.7f+i)*3+2;add(2,{std::sin(a)*r,y,std::cos(a)*r},{1.8f,.6f+f.spectrum[i%24]*.6f,1.3f},a+t*.1f,t*.07f+i,stone,.35f,.4f);}
  for(int i=0;i<180;++i){float a=i*2.39996f+t*.35f,r=5+std::fmod(i*.73f,14.f);add(2,{std::sin(a)*r,2+std::sin(i+t)*6,std::cos(a)*r},{.12f,.12f,.4f},a,t*.3f,steel,.8f,.2f);}
 }else{
  // Crystal wings, corridor ribs and orbital fleet reunite for the final ascent.
  for(int ring=0;ring<6;++ring){float r=3.4f+ring*.8f;add(3,{0,3.4f,0},{r,r,4},t*.15f+ring*.4f,.22f*std::sin(t*.3f+ring),ring%2?gold:steel,.9f,.1f,{},0,ring*.3f);}
  add(2,{0,3.6f,0},{2,4,2},t*.25f,.1f,{.4f,.85f,1},.1f,.04f,{},.95f);
  for(int side:{-1,1})for(int j=0;j<36;++j){float x=side*(2.5f+j*.24f),y=3.8f+std::sin(j*.09f+t*.5f)*2,z=std::sin(j*.12f+t*.2f)*1.4f;V3 e=j%2?cyan:gold;add(2,{x,y,z},{.16f,1.2f+f.spectrum[j%24],.8f},t*.12f,side*.4f,e,.8f,.12f,e*.25f);}
  for(int i=0;i<140;++i){float a=i*2.39996f+t*.2f,r=30+std::fmod(i*1.37f,24.f);V3 p{std::sin(a)*r,6+std::sin(i+t*.4f)*8,std::cos(a)*r};add(0,p,{.15f,.07f,.65f},a,0,steel,.85f,.2f);add(1,p+V3{0,0,-.6f},{.09f,.09f,.09f},0,0,cyan,0,.3f,cyan*(3+f.hit*4));}
 }
}
