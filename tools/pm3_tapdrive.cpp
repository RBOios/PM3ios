// pm3_tapdrive <game_dir> <save_dir> "x,y,wait_s;x,y,wait_s;..."  : scripted taps through the bridge, frames saved to /tmp/drv_N.ppm (N=0 before the first tap). Used to reproduce UI flows headlessly.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
#include "pm3/bridge.h"
static void save(const char* p, std::vector<uint32_t>& fb){FILE*f=fopen(p,"wb");fprintf(f,"P6\n640 480\n255\n");for(uint32_t v:fb){uint8_t c[3]={(uint8_t)(v>>16),(uint8_t)(v>>8),(uint8_t)v};fwrite(c,1,3,f);}fclose(f);}
int main(int argc,char**argv){
  Pm3Bridge*b=pm3_bridge_create(); pm3_bridge_set_paths(b,argv[1],argv[2]); pm3_bridge_set_view_size(b,640,480,0);
  if(!pm3_bridge_start_game(b)){puts("nostart");return 1;}
  std::vector<uint32_t> fr(640*480); std::this_thread::sleep_for(std::chrono::seconds(6)); pm3_bridge_copy_frame(b,fr.data()); save("/tmp/drv_0.ppm",fr);
  int n=1; char* s=strdup(argv[3]); for(char*t=strtok(s,";");t;t=strtok(0,";")){ int x,y; double w; sscanf(t,"%d,%d,%lf",&x,&y,&w);
    pm3_bridge_touch_begin(b,1,(float)x,(float)y); std::this_thread::sleep_for(std::chrono::milliseconds(120)); pm3_bridge_touch_end(b,1,(float)x,(float)y);
    std::this_thread::sleep_for(std::chrono::milliseconds((int)(w*1000))); pm3_bridge_copy_frame(b,fr.data()); char p[64]; sprintf(p,"/tmp/drv_%d.ppm",n++); save(p,fr);
    printf("tap %d,%d -> running=%d\n",x,y,pm3_bridge_game_running(b)); fflush(stdout);}
  pm3_bridge_stop_game(b); return 0;}
