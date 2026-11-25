// pm3_tapdrive <game_dir> <save_dir> "x,y,wait_s;x,y,wait_s;..."  : scripted taps through the bridge, frames saved to /tmp/drv_N.ppm (N=0 before the first tap). Used to reproduce UI flows headlessly.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include "pm3/bridge.h"
static void save(const char* p, std::vector<uint32_t>& fb){FILE*f=fopen(p,"wb");fprintf(f,"P6\n640 480\n255\n");for(uint32_t v:fb){uint8_t c[3]={(uint8_t)(v>>16),(uint8_t)(v>>8),(uint8_t)v};fwrite(c,1,3,f);}fclose(f);}
static Pm3Bridge* g_b; static std::vector<int16_t> g_au;   // PM3_WAV=<file>: everything the audio callback would have pulled, saved as 44.1 kHz stereo WAV
static void nap(int ms){ for(int t=0;t<ms;t+=10){ int16_t buf[441*2]; pm3_bridge_pull_audio(g_b,buf,441); g_au.insert(g_au.end(),buf,buf+882); std::this_thread::sleep_for(std::chrono::milliseconds(10)); } }
static void save_wav(const char* path){ FILE* f=fopen(path,"wb"); if(!f) return; uint32_t n=(uint32_t)(g_au.size()*2), rate=44100, br=rate*4, c16=16, riff=36+n; uint16_t ch=2,bits=16,ba=4,fmt=1;
  fwrite("RIFF",1,4,f); fwrite(&riff,4,1,f); fwrite("WAVEfmt ",1,8,f); fwrite(&c16,4,1,f); fwrite(&fmt,2,1,f); fwrite(&ch,2,1,f); fwrite(&rate,4,1,f); fwrite(&br,4,1,f); fwrite(&ba,2,1,f); fwrite(&bits,2,1,f); fwrite("data",1,4,f); fwrite(&n,4,1,f); fwrite(g_au.data(),2,g_au.size(),f); fclose(f); }
int main(int argc,char**argv){
  Pm3Bridge*b=pm3_bridge_create(); g_b=b; pm3_bridge_set_paths(b,argv[1],argv[2]); pm3_bridge_set_view_size(b,640,480,0);
  if(!pm3_bridge_start_game(b)){puts("nostart");return 1;}
  std::vector<uint32_t> fr(640*480); nap(6000); pm3_bridge_copy_frame(b,fr.data()); save("/tmp/drv_0.ppm",fr);
  int n=1; char* s=strdup(argv[3]); for(char*t=strtok(s,";");t;t=strtok(0,";")){ int x,y,r=0; double w; sscanf(t,"%d,%d,%lf,%d",&x,&y,&w,&r);   // 4th field 1 = right click
    if(r){ pm3_bridge_right_click(b,(float)x,(float)y); } else { pm3_bridge_touch_begin(b,1,(float)x,(float)y); std::this_thread::sleep_for(std::chrono::milliseconds(120)); pm3_bridge_touch_end(b,1,(float)x,(float)y); }
    nap((int)(w*1000)); pm3_bridge_copy_frame(b,fr.data()); char p[64]; sprintf(p,"/tmp/drv_%d.ppm",n++); save(p,fr);
    printf("tap %d,%d -> running=%d\n",x,y,pm3_bridge_game_running(b)); fflush(stdout);}
  if(const char* wp=getenv("PM3_WAV")) save_wav(wp);
  pm3_bridge_stop_game(b); return 0;}
