// Origin: C:\Users\r3muxd\tmp\r2port\bspdown. Bundled in r1delta under GPL-3.0.
// Focused production rule-20 tests. Build instead of main.cpp and lighting.cpp,
// linking the remaining bspdown sources. Kept beside lighting.cpp in the source tree.
#include "lighting.cpp"
#include <iostream>
namespace bspdown { namespace detail { namespace {
struct Header { uint32_t type; uint16_t width, height; };
using Triangle = std::array<Vec3, 3>;
using Uvs = std::array<std::array<float, 2>, 3>;
const Triangle farReceiver{{{10,0,0}, {11,0,0}, {10,1,0}}};
const Triangle nearReceiver{{{0,0,0}, {1,0,0}, {0,1,0}}};
const Uvs chart{{{{.2f/4,.2f/4}}, {{3.9f/4,.2f/4}}, {{.2f/4,3.4f/4}}}};
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void setType(Light& l, int32_t value) { std::memcpy(l.b + kOffType, &value, 4); }
bool contains(const Report& r, const char* text) {
    return std::any_of(r.lines.begin(), r.lines.end(), [text](const std::string& s) { return s.find(text) != std::string::npos; });
}
struct Fixture {
    Ctx c; Report report;
    std::vector<Light> lights{Light{}};
    std::vector<uint16_t> ids = std::vector<uint16_t>(64, 0xFFFF);
    std::vector<double> usage{0.0};
    double linear[256]; uint64_t rejected = 0, accepted = 0;
    std::vector<MeshR2> meshes; std::vector<Sort> sorts;
    std::vector<Vec3> positions; std::vector<uint16_t> indices; Bytes vertices;
    Fixture() {
        c.rep = &report;
        c.lump[L_LMHDR] = pack(std::vector<Header>{{1,4,4}}); c.lump[L_LMRTL].resize(144);
        Light& l = lights[0]; setType(l, 2); l.setFlags(8);
        l.setF(8,1); l.setF(32,-1); l.setF(60,1); l.setF(64,0); l.setF(68,1);
        for (int i=0; i<256; ++i) { double v=i/255.0; linear[i]=v<=.04045 ? v/12.92 : std::pow((v+.055)/1.055,2.4); }
    }
    void reference(int x,int y,uint8_t w) { size_t t=size_t(y*4+x); ids[t*4]=0; c.lump[L_LMRTL][64+t*4]=w; }
    bool kept(int x,int y) const { return ids[size_t(y*4+x)*4]!=0xFFFF; }
    void receiver(const Triangle& t,const Uvs& uv=chart,int header=0) {
        MeshR2 m{}; m.firstMeshIndex=(uint32_t)indices.size(); m.numTriangles=1; m.materialSort=(uint16_t)sorts.size(); meshes.push_back(m);
        Sort s{}; s.lightmapHeader=(int16_t)header; s.vertexOffset=(int32_t)positions.size(); sorts.push_back(s);
        for (int k=0;k<3;++k) {
            uint32_t p=(uint32_t)positions.size(); positions.push_back(t[k]); indices.push_back((uint16_t)k);
            size_t b=vertices.size(); vertices.resize(b+36); std::memcpy(&vertices[b],&p,4); std::memcpy(&vertices[b+20],uv[k].data(),8);
        }
    }
    void run() {
        c.lump[L_MESHES]=pack(meshes); c.lump[L_SORTS]=pack(sorts); c.lump[L_VERTICES]=pack(positions);
        c.lump[L_MESHIDX]=pack(indices); c.lump[L_VTX_LITFLAT]=vertices;
        quantizeRtlVisibility(c,lights,ids,linear,usage,rejected,accepted);
    }
};
void runTests() {
    int cases=0;
    { Fixture f; setType(f.lights[0],1); f.reference(0,0,66); f.reference(1,0,255); f.run();
      require(!f.kept(0,0)&&f.kept(1,0)&&f.accepted==1&&f.rejected==1,"point threshold changed or requires UV geometry"); ++cases; }
    { Fixture f; f.reference(0,0,0); f.run();
      require(!f.kept(0,0)&&f.accepted==0,"B=0 survives the visibility cutoff"); ++cases; }
    { Fixture f; f.receiver(farReceiver); f.reference(1,1,90); f.run();
      require(f.kept(1,1)&&contains(f.report,"1 centre-covered"),"valid cone tail lost"); ++cases; }
    { Fixture f; f.receiver(farReceiver); f.reference(2,1,90); f.run();
      require(f.kept(2,1)&&contains(f.report,"1 sampled chart-border footprints"),"reachable border receiver lost"); ++cases; }
    { Fixture f; f.receiver(farReceiver); f.reference(3,3,90); f.run();
      require(!f.kept(3,3)&&contains(f.report,"1 unreachable"),"unreachable texel dilated into receiver"); ++cases; }
    { Fixture f; f.receiver(farReceiver); f.reference(1,1,1); f.lights[0].setF(32,1); f.run();
      require(f.kept(1,1)&&rtlSpotCone(f.lights[0],{{10,0,0}})==0,"zero-cone unsafe evaluation"); ++cases; }
    { Fixture f; const Uvs uv{{{{0,0}},{{1,0}},{{0,1}}}}; const std::array<double,3> p{{10.375,.375,0}};
      double cosine=1/std::sqrt(p[0]*p[0]+p[1]*p[1]+1); f.lights[0].setF(64,(float)(cosine-1e-7)); double cone=rtlSpotCone(f.lights[0],p);
      require(std::isfinite(cone)&&cone>0&&cone<1e-4,"tiny-cone fixture invalid"); f.receiver(farReceiver,uv); f.reference(1,1,1); f.run();
      require(f.kept(1,1),"near-zero positive cone lost receiver"); ++cases; }
    { for (bool reverse:{false,true}) { Fixture f; f.receiver(reverse?nearReceiver:farReceiver); f.receiver(reverse?farReceiver:nearReceiver);
        f.reference(1,1,90); f.run(); require(!f.kept(1,1)&&contains(f.report,"1 aliased texels")&&contains(f.report,"1 disagreeing slot decisions"),"alias not conservative/order-independent"); } ++cases; }
    { Fixture f; f.receiver(farReceiver); f.receiver(farReceiver); f.reference(1,1,90); f.run();
      require(f.kept(1,1)&&contains(f.report,"0 disagreeing slot decisions"),"identical shared receivers disagree"); ++cases; }
    { Fixture f; f.c.lump[L_LMHDR]=pack(std::vector<Header>{{0,4,4},{1,4,4}}); f.receiver(farReceiver,chart,1); f.reference(1,1,90); f.run();
      require(f.kept(1,1),"header index confused with type1 rank"); ++cases; }
    { Fixture f; const Uvs uv{{{{.375f,.375f}},{{.375f,.375f}},{{.375f,.375f}}}}; f.receiver(farReceiver,uv); f.reference(1,1,90); bool refused=false;
      try { f.run(); } catch(const Error& e) { refused=std::string(e.what()).find("non-invertible")!=std::string::npos; }
      require(refused,"reachable degenerate UV silently falls back"); ++cases; }
    {
      Ctx c; Report report; c.rep=&report; std::vector<Light> lights(257);
      for(Light& l:lights) { setType(l,1); l.setFlags(8); l.setF(12,1); l.setF(16,1); l.setF(20,1); l.setF(kOffRadius,100); l.setF(kOffC,1); l.setF(kOffL,-.01f); }
      setType(lights[0],5); lights[0].setFlags(0); setType(lights[1],3); lights[1].setFlags(0);
      c.version[L_WORLDLIGHTS]=3; c.lump[L_WORLDLIGHTS].resize(lights.size()*112);
      for(size_t i=0;i<lights.size();++i) std::memcpy(&c.lump[L_WORLDLIGHTS][i*112],lights[i].b,100);
      c.lump[L_LMHDR]=pack(std::vector<Header>{{1,1,1}}); c.lump[L_LMRTL].resize(9);
      uint32_t a=(1u<<6)|(2u<<12)|(63u<<18); std::memcpy(c.lump[L_LMRTL].data(),&a,4);
      c.lump[L_LMRTL][4]=c.lump[L_LMRTL][5]=c.lump[L_LMRTL][6]=255;
      std::vector<uint16_t> palette(63,0xFFFF); palette[0]=253; palette[1]=254; palette[2]=255; c.lump[0x7A]=pack(palette);
      convertLighting(c); const auto converted=view<Light>(c.lump[L_WORLDLIGHTS],"test worldlights");
      require((converted[254].flags()&8)&&!(converted[255].flags()&8)&&!(converted[256].flags()&8),"flag boundary254/255/256 inconsistent");
      require(c.lump[L_LMRTL].size()==4&&c.lump[L_LMRTL][3]==255,"four-slot list not terminated");
      for(int s=0;s<3;++s) { int world=c.lump[L_LMRTL][s]+1; require(world<255&&(converted[world].flags()&8)&&converted[world].type()==1,"emitted reference targets cleared flag/bad index"); }
      ++cases;
    }
    std::cout<<"RTL visibility: "<<cases<<" production fixture cases passed\n";
}
} } }
int main() { try { bspdown::detail::runTests(); return 0; } catch(const std::exception& e) { std::cerr<<"RTL fixture failure: "<<e.what()<<'\n'; return 1; } }
