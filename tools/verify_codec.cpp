#include "../lib/CardputerMirror/Codec.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <random>
using namespace cmirror;
// Geometry mirrored from CardputerMirror.h (that header needs Arduino.h,
// which is unavailable in a native build). Kept in sync by the asserts below.
static constexpr int kScreenW=240, kScreenH=135, kTileW=60, kTileH=45;
static constexpr size_t kTilePx=(size_t)kTileW*kTileH;
static constexpr size_t kShadowBytes=(size_t)kScreenW*kScreenH*2;
static_assert(kScreenW%kTileW==0 && kScreenH%kTileH==0,"tiles must divide exactly");
static_assert(kTilePx==2700,"tile px");
static_assert(kShadowBytes==64800,"shadow bytes");
static bool decode(const uint8_t* in, size_t n, std::vector<uint16_t>& out){
  if(n<3) return false; uint8_t enc=in[0]; size_t cnt=in[1]|(in[2]<<8); out.clear();
  if(enc==kEncRaw){ if(n<3+cnt*2) return false;
    for(size_t i=0;i<cnt;i++) out.push_back(in[3+i*2]|(in[3+i*2+1]<<8)); return true; }
  if(enc==kEncRle){ if(n<3+cnt*3) return false;
    for(size_t i=0;i<cnt;i++){ uint16_t v=in[3+i*3]|(in[3+i*3+1]<<8); uint8_t rl=in[3+i*3+2];
      if(rl==0) return false; for(int k=0;k<rl;k++) out.push_back(v);} return true; }
  return false;
}
int main(){
  std::mt19937 rng(12345); size_t worst=0; long tot=0,totRaw=0; int fail=0;
  const size_t N=kTilePx; std::vector<uint8_t> buf(N*3+64);
  for(int trial=0; trial<600; ++trial){
    std::vector<uint16_t> px(N);
    int mode=trial%4;
    for(size_t i=0;i<N;i++){
      if(mode==0) px[i]=0x0000;                                  // flat
      else if(mode==1) px[i]=(i/kTileW<20)?0xFFFF:0x001F;        // banded
      else if(mode==2) px[i]=(rng()%17==0)?(uint16_t)rng():0x0000;// sparse text
      else px[i]=(uint16_t)rng();                                // noise
    }
    size_t n=encodeTile(px.data(),N,buf.data(),buf.size());
    if(n==0){ printf("FAIL encode trial=%d\n",trial); fail++; continue; }
    std::vector<uint16_t> back;
    if(!decode(buf.data(),n,back)||back.size()!=N||back!=px){ printf("FAIL roundtrip trial=%d mode=%d\n",trial,mode); fail++; continue; }
    if(n>worst) worst=n; tot+=n; totRaw+=N*2;
  }
  // capacity guard: encoder must refuse rather than overflow
  std::vector<uint16_t> px(N); for(size_t i=0;i<N;i++) px[i]=(uint16_t)rng();
  if(encodeTile(px.data(),N,buf.data(),8)!=0){ printf("FAIL: overflowed small buffer\n"); fail++; }
  printf("tilePx=%zu shadow=%zu B worstEncoded=%zu B rawTile=%zu B\n",N,kShadowBytes,worst,N*2);
  printf("avg ratio=%.3f  failures=%d\n",(double)tot/(double)totRaw,fail);
  printf("crc32 sanity=%08X (nonzero:%d)\n",crc32(px.data(),16),crc32(px.data(),16)!=0);
  return fail?1:0;
}
