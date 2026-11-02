#include "platform.hpp"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#if defined(LAUNCHER_DEVICE)
#include <linux/fb.h>
#include <sys/ioctl.h>
#endif

// OnionOS devices and their panels:
//   Miyoo Mini V1-V3   2.8"  640x480
//   Miyoo Mini V4      2.8"  752x560
//   Miyoo Mini Flip    2.8"  750x560
//   Miyoo Mini Plus    3.5"  640x480 (physically larger, same resolution as V1-V3)

namespace {
std::string env(const char* name){
    const char* value=std::getenv(name);
    return value?value:"";
}

int envInt(const char* name){
    const int parsed=std::atoi(env(name).c_str());
    return parsed>0?parsed:0;
}

#if defined(LAUNCHER_DEVICE)
// OnionOS writes the physical panel size here, e.g. "752x560".
bool onionScreenResolution(int& width,int& height){
    FILE* file=std::fopen("/tmp/screen_resolution","r");
    if(!file) return false;
    int w=0,h=0;
    const bool ok=std::fscanf(file,"%dx%d",&w,&h)==2 && w>0 && h>0;
    std::fclose(file);
    if(ok){width=w;height=h;}
    return ok;
}

// Fallback for older OnionOS releases: the framebuffer's current mode.
void framebufferSize(int& width,int& height){
    const int framebuffer=open("/dev/fb0",O_RDONLY);
    if(framebuffer<0) return;
    fb_var_screeninfo mode{};
    if(ioctl(framebuffer,FBIOGET_VSCREENINFO,&mode)==0 && mode.xres>0 && mode.yres>0){
        width=static_cast<int>(mode.xres);
        height=static_cast<int>(mode.yres);
    }
    close(framebuffer);
}
#endif
}

namespace platform {
Platform detect(){
    Platform p;
    p.sdRoot=env("CARTOS_SD_ROOT");
    while(p.sdRoot.size()>1 && p.sdRoot.back()=='/') p.sdRoot.pop_back();
    if(p.sdRoot.empty()) p.sdRoot="/mnt/SDCARD";

    p.screenW=envInt("CARTOS_SCREEN_W");
    p.screenH=envInt("CARTOS_SCREEN_H");
#if defined(LAUNCHER_DEVICE)
    if((p.screenW==0 || p.screenH==0) && !onionScreenResolution(p.screenW,p.screenH))
        framebufferSize(p.screenW,p.screenH);
#endif

    p.id=env("CARTOS_PLATFORM");
    if(p.id.empty()){
#if defined(LAUNCHER_DEVICE)
        // OnionOS identifies the Mini Plus by its AXP power-chip test tool.
        p.id=access("/customer/app/axp_test",F_OK)==0?"miyoo-mini-plus":"miyoo-mini";
#else
        p.id="desktop";
#endif
    }
    if(p.id=="miyoo-mini-plus") p.name="Miyoo Mini Plus";
    else if(p.id=="miyoo-mini") p.name=p.screenH>=560?"Miyoo Mini V4 / Mini Flip":"Miyoo Mini";
    else if(p.id=="desktop") p.name="Desktop preview";
    else p.name=p.id;
    return p;
}

int batteryPercent(){
    // OnionOS's batmon service keeps the current charge here.
    if(FILE* file=std::fopen("/tmp/percBat","r")){
        int value=-1;
        const bool ok=std::fscanf(file,"%d",&value)==1 && value>=0 && value<=100;
        std::fclose(file);
        if(ok) return value;
    }
    int percent=-1;
    SDL_GetPowerInfo(nullptr,&percent);
    return percent>=0 && percent<=100?percent:-1;
}
}
