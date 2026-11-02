#include "platform.hpp"
#include <SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <unistd.h>
#if defined(LAUNCHER_DEVICE)
#include <linux/fb.h>
#include <sys/ioctl.h>
#endif

namespace {
std::string env(const char* name){
    const char* value=std::getenv(name);
    return value?value:"";
}

int envInt(const char* name){
    const std::string value=env(name);
    if(value.empty()) return 0;
    const int parsed=std::atoi(value.c_str());
    return parsed>0?parsed:0;
}

bool exists(const std::string& path){
    return access(path.c_str(),F_OK)==0;
}

std::string trimmed(const std::string& path){
    std::ifstream file(path.c_str());
    if(!file) return {};
    std::string value;
    std::getline(file,value);
    value.erase(std::remove_if(value.begin(),value.end(),[](unsigned char c){return std::isspace(c);}),value.end());
    return value;
}

bool readPercent(const std::string& path,int& percent){
    std::istringstream input(trimmed(path));
    int value=-1;
    if(input>>value && value>=0 && value<=100){percent=value;return true;}
    return false;
}

#if defined(LAUNCHER_DEVICE)
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
    if(p.screenW==0 || p.screenH==0) framebufferSize(p.screenW,p.screenH);
#endif

    p.id=env("CARTOS_PLATFORM");
    if(p.id.empty()){
#if defined(LAUNCHER_DEVICE)
        // OnionOS identifies the Mini Plus by its AXP power-chip test tool.
        if(exists("/customer/app/axp_test")) p.id="miyoo-mini-plus";
        else if(exists("/customer/app/MainUI") || exists("/mnt/SDCARD/miyoo")) p.id="miyoo-mini";
        else p.id="linux-handheld";
#else
        p.id="desktop";
#endif
    }
    if(p.id=="miyoo-mini-plus") p.name="Miyoo Mini Plus";
    else if(p.id=="miyoo-mini") p.name=p.screenH>=560?"Miyoo Mini V4 / Mini Flip (560p)":"Miyoo Mini";
    else if(p.id=="desktop") p.name="Desktop preview";
    else p.name=p.id;
    return p;
}

int batteryPercent(){
    int percent=-1;
    // OnionOS keeps the current charge here (written by its keymon service).
    if(readPercent("/tmp/percBat",percent)) return percent;
    // spruceOS devices, Anbernic and Powkiddy handhelds use the kernel's
    // power-supply class instead.
    if(DIR* directory=opendir("/sys/class/power_supply")){
        while(const dirent* entry=readdir(directory)){
            const std::string name=entry->d_name;
            if(name=="." || name=="..") continue;
            const std::string base="/sys/class/power_supply/"+name+"/";
            std::string type=trimmed(base+"type");
            std::transform(type.begin(),type.end(),type.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            std::string lowerName=name;
            std::transform(lowerName.begin(),lowerName.end(),lowerName.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            if(type!="battery" && lowerName.find("battery")==std::string::npos) continue;
            if(readPercent(base+"capacity",percent)) break;
        }
        closedir(directory);
        if(percent>=0) return percent;
    }
    SDL_GetPowerInfo(nullptr,&percent);
    return percent>=0 && percent<=100?percent:-1;
}
}
