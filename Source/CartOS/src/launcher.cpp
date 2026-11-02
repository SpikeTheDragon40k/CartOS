
#include "launcher.hpp"
#include "artwork.hpp"
#include "draw.hpp"
#include "platform.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <limits.h>
#include <sstream>
#include <sys/stat.h>
#include <unordered_set>
#include <unistd.h>
#if defined(LAUNCHER_DEVICE)
#include <linux/fb.h>
#include <sys/ioctl.h>
#endif

namespace {
constexpr int kDesignW=752, kDesignH=560;
constexpr float kLaunchAnimationDuration=1.20f;
constexpr float kLaunchCenterDuration=0.16f;
constexpr float kLaunchReverseDuration=0.32f;
constexpr Uint64 kLaunchDoubleTapWindowMs=380;
constexpr Uint64 kScrollInitialDelayMs=360;
constexpr Uint64 kScrollRepeatIntervalMs=180;
void fillRect(SDL_Renderer* renderer,const SDL_Rect& rect,SDL_Color color);
void outlineRect(SDL_Renderer* renderer,const SDL_Rect& rect,SDL_Color color);
std::string shortLabel(std::string value,size_t limit);
constexpr const char* kSearchKeyboardRows[]={
    "ABCDEFGHIJ",
    "KLMNOPQRST",
    "UVWXYZ0123",
    "456789 -'."
};
SDL_Color white{240,240,240,255}, muted{125,128,134,255};

char searchKeyboardCharacter(int index){
    const int clamped=std::max(0,std::min(39,index));
    return kSearchKeyboardRows[clamped/10][clamped%10];
}

int wrapIndex(int index,int count){
    index%=count;
    return index<0 ? index+count : index;
}

std::string lower(std::string value){
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return value;
}

bool supportedRom(const std::string& filename,const SystemInfo& system){
    const size_t dot=filename.find_last_of('.');
    if(dot==std::string::npos || dot==0) return false;
    const std::string ext=lower(filename.substr(dot));
    return std::find(system.extensions.begin(),system.extensions.end(),ext)!=system.extensions.end();
}

bool isUtilityEntry(const std::string& title,const std::string& filename){
    auto isUtilityName=[](std::string name){
        const size_t first=name.find_first_not_of(" \t\r\n");
        if(first==std::string::npos) return false;
        const size_t last=name.find_last_not_of(" \t\r\n");
        name=name.substr(first,last-first+1);
        const size_t extension=name.find_last_of('.');
        if(extension!=std::string::npos) name.resize(extension);
        name=lower(name);
        return name=="~filter" || name=="~refresh roms" || name=="~search" || name=="search";
    };
    return isUtilityName(title) || isUtilityName(filename);
}

std::string jsonString(const std::string& text,const std::string& key){
    const std::string marker="\""+key+"\"";
    size_t pos=text.find(marker);
    if(pos==std::string::npos) return {};
    pos=text.find(':',pos+marker.size());
    if(pos==std::string::npos) return {};
    ++pos;
    while(pos<text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if(pos>=text.size() || text[pos]!='\"') return {};
    ++pos;
    std::string value;
    bool escaped=false;
    for(;pos<text.size();++pos){
        const char c=text[pos];
        if(escaped){ value.push_back(c); escaped=false; }
        else if(c=='\\') escaped=true;
        else if(c=='\"') break;
        else value.push_back(c);
    }
    return value;
}

std::string readText(const std::string& path){
    std::ifstream file(path.c_str());
    if(!file) return {};
    std::ostringstream contents;
    contents<<file.rdbuf();
    return contents.str();
}

std::string sdPath(const std::string& path,const std::string& sdRoot);

std::vector<std::string> splitPipeList(const std::string& value){
    std::vector<std::string> result;
    std::istringstream input(value);
    std::string item;
    while(std::getline(input,item,'|')){
        const size_t first=item.find_first_not_of(" \t\r\n");
        if(first==std::string::npos) continue;
        const size_t last=item.find_last_not_of(" \t\r\n");
        item=item.substr(first,last-first+1);
        if(!item.empty()) result.push_back(item);
    }
    return result;
}

std::string resolveConfigPath(const std::string& value,const std::string& sdRoot,
                              const std::string& relativeTo){
    if(value.empty()) return {};
    if(value[0]=='/') return sdPath(value,sdRoot);
    return relativeTo+"/"+value;
}

std::string sdPath(const std::string& path,const std::string& sdRoot){
    static const std::string deviceRoot="/mnt/SDCARD";
    if(path.compare(0,deviceRoot.size(),deviceRoot)==0 &&
       (path.size()==deviceRoot.size() || path[deviceRoot.size()]=='/'))
        return sdRoot+path.substr(deviceRoot.size());
    return path;
}

std::string jsonEscape(const std::string& value);

std::string onionPath(const std::string& path,const std::string& sdRoot){
    if(path.compare(0,sdRoot.size(),sdRoot)==0 &&
       (path.size()==sdRoot.size() || path[sdRoot.size()]=='/'))
        return std::string("/mnt/SDCARD")+path.substr(sdRoot.size());
    return path;
}

std::string serializedListEntry(const Game& game,const std::string& sdRoot){
    const std::string romMarker="/Roms/";
    const size_t romsPos=game.romPath.find(romMarker);
    const size_t launcherSlash=game.launcherPath.find_last_of('/');
    if(romsPos==std::string::npos || launcherSlash==std::string::npos) return {};
    const std::string emuDir=onionPath(game.launcherPath.substr(0,launcherSlash),sdRoot);
    const std::string relativeRom=game.romPath.substr(romsPos+romMarker.size());
    const std::string romPath=emuDir+"/../../Roms/"+relativeRom;
    std::string imagePath;
    const size_t imagePos=game.artPath.find(romMarker);
    if(imagePos!=std::string::npos)
        imagePath=emuDir+"/../../Roms/"+game.artPath.substr(imagePos+romMarker.size());
    return "{\"label\":\""+jsonEscape(game.title)+"\",\"launch\":\""+
           jsonEscape(onionPath(game.launcherPath,sdRoot))+"\",\"type\":5,\"imgpath\":\""+
           jsonEscape(imagePath)+"\",\"rompath\":\""+jsonEscape(romPath)+"\"}";
}

std::string comparableRomPath(const std::string& path,const std::string& sdRoot){
    const std::string converted=sdPath(path,sdRoot);
    std::vector<std::string> components;
    std::istringstream input(converted);
    std::string component;
    while(std::getline(input,component,'/')){
        if(component.empty() || component==".") continue;
        if(component=="..") {if(!components.empty()) components.pop_back();continue;}
        components.push_back(component);
    }
    std::string normalized;
    for(const std::string& item:components) normalized+="/"+item;
    return normalized.empty()?"/":normalized;
}

bool regularFile(const std::string& path){
    struct stat info{};
    return stat(path.c_str(),&info)==0 && S_ISREG(info.st_mode);
}

std::string filenameWithoutExtension(const std::string& path){
    const size_t slash=path.find_last_of('/');
    std::string name=slash==std::string::npos?path:path.substr(slash+1);
    const size_t dot=name.find_last_of('.');
    if(dot!=std::string::npos && dot>0) name.resize(dot);
    return name;
}

std::string shellDoubleQuote(const std::string& value){
    std::string quoted="\"";
    for(char c:value){
        if(c=='\\' || c=='\"' || c=='$' || c=='`') quoted.push_back('\\');
        quoted.push_back(c);
    }
    quoted.push_back('\"');
    return quoted;
}

std::string shellPathArgument(const std::string& path){
    const bool simple=std::all_of(path.begin(),path.end(),[](unsigned char c){
        return std::isalnum(c) || c=='/' || c=='.' || c=='_' || c=='-';
    });
    return simple?path:shellDoubleQuote(path);
}

std::string onionAppLaunchCommand(const std::string& launcher,
                                  const std::string& workingDirectory,
                                  const std::string& sdRoot,
                                  bool musicPlayer){
    const std::string appDirectory=onionPath(workingDirectory,sdRoot);
    const std::string deviceLauncher=onionPath(launcher,sdRoot);
    std::string launchTarget=deviceLauncher;
    if(!workingDirectory.empty() && launcher.compare(0,workingDirectory.size(),workingDirectory)==0 &&
       launcher.size()>workingDirectory.size() && launcher[workingDirectory.size()]=='/')
        launchTarget="./"+launcher.substr(workingDirectory.size()+1);

    std::string command;
    // OnionOS 4.3.1 inspects the second whitespace-delimited field in this
    // command to locate an app's optional full_resolution marker. Keep a
    // plain safe path unquoted so that runtime.sh can parse the same command
    // shape MainUI emits. shellPathArgument still quotes paths with spaces or
    // shell-sensitive characters.
    if(!appDirectory.empty()) command="cd "+shellPathArgument(appDirectory)+"; ";
    command+="chmod a+x "+shellPathArgument(launchTarget)+"; ";
    // GMU (OnionOS Music Player) stops OnionOS's audio server and starts its
    // own SDL 1.2 stack. Give the freshly handed-off runtime a moment to
    // settle before GMU performs that transition. Other apps keep the normal
    // MainUI-compatible command with no added delay.
    if(musicPlayer) command+="sleep 1; ";
    command+="LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so "+
             shellPathArgument(launchTarget);
    return command;
}

std::string launchInvocation(const std::string& path){
    const size_t dot=path.find_last_of('.');
    if(dot!=std::string::npos && lower(path.substr(dot))==".sh")
        return "/bin/sh "+shellDoubleQuote(path);
    return shellDoubleQuote(path);
}

std::string jsonEscape(const std::string& value){
    std::string escaped;
    escaped.reserve(value.size()+8);
    for(unsigned char c:value){
        switch(c){
            case '"': escaped+="\\\""; break;
            case '\\': escaped+="\\\\"; break;
            case '\b': escaped+="\\b"; break;
            case '\f': escaped+="\\f"; break;
            case '\n': escaped+="\\n"; break;
            case '\r': escaped+="\\r"; break;
            case '\t': escaped+="\\t"; break;
            default:
                if(c<0x20){
                    static const char hex[]="0123456789abcdef";
                    escaped+="\\u00";
                    escaped.push_back(hex[(c>>4)&0xf]);
                    escaped.push_back(hex[c&0xf]);
                }else escaped.push_back(static_cast<char>(c));
        }
    }
    return escaped;
}

bool writeOnionRecentGame(const Game& game){
    const std::string deviceRoot="/mnt/SDCARD";
    const std::string romsMarker="/Roms/";
    const size_t romsPos=game.romPath.find(romsMarker);
    const size_t launcherSlash=game.launcherPath.find_last_of('/');
    if(romsPos==std::string::npos || launcherSlash==std::string::npos) return false;

    const std::string emuDir=game.launcherPath.substr(0,launcherSlash);
    const std::string relativeRom=game.romPath.substr(romsPos+romsMarker.size());
    const std::string recentRomPath=emuDir+"/../../Roms/"+relativeRom;
    std::string recentImagePath;
    const size_t imageRomsPos=game.artPath.find(romsMarker);
    if(imageRomsPos!=std::string::npos)
        recentImagePath=emuDir+"/../../Roms/"+game.artPath.substr(imageRomsPos+romsMarker.size());

    const std::string recentFile=deviceRoot+"/Roms/recentlist.json";
    const std::string temporaryFile=recentFile+".cartos.tmp";
    const std::string newLine="{\"label\":\""+jsonEscape(game.title)+"\",\"rompath\":\""+
                             jsonEscape(recentRomPath)+"\",\"imgpath\":\""+jsonEscape(recentImagePath)+
                             "\",\"launch\":\""+jsonEscape(game.launcherPath)+"\",\"type\":5}\n";
    {
        std::ofstream output(temporaryFile.c_str(),std::ios::out|std::ios::trunc);
        if(!output) return false;
        output<<newLine;
        std::ifstream existing(recentFile.c_str());
        std::string line;
        while(std::getline(existing,line)){
            if(jsonString(line,"rompath")==recentRomPath) continue;
            output<<line<<'\n';
        }
        output.close();
        if(!output){unlink(temporaryFile.c_str()); return false;}
    }
    if(rename(temporaryFile.c_str(),recentFile.c_str())!=0){
        unlink(temporaryFile.c_str());
        return false;
    }
    return true;
}

#if defined(LAUNCHER_DEVICE)
void resetFramebufferToTopPage(){
    const int framebuffer=open("/dev/fb0",O_RDWR);
    if(framebuffer<0) return;
    fb_var_screeninfo mode{};
    if(ioctl(framebuffer,FBIOGET_VSCREENINFO,&mode)==0 && mode.yoffset!=0){
        mode.yoffset=0;
        mode.activate=FB_ACTIVATE_NOW;
        ioctl(framebuffer,FBIOPAN_DISPLAY,&mode);
    }
    close(framebuffer);
}
#endif

void clearArtwork(std::vector<Game>& entries){
    for(Game& entry:entries){
        if(entry.artwork){ SDL_DestroyTexture(entry.artwork); entry.artwork=nullptr; }
        entry.artworkAttempted=false;
    }
}
}

Launcher::Launcher() = default;

Category* Launcher::currentCategory(){
    return categories_.empty()?nullptr:&categories_[categoryIndex_];
}
const Category* Launcher::currentCategory() const{
    return categories_.empty()?nullptr:&categories_[categoryIndex_];
}

size_t Launcher::adjacentVisibleCategory(int direction) const{
    if(categories_.empty()) return 0;
    const int count=static_cast<int>(categories_.size());
    int candidate=static_cast<int>(categoryIndex_);
    for(int step=0;step<count;++step){
        candidate=wrapIndex(candidate+direction,count);
        if(!systemHiddenFor(categories_[static_cast<size_t>(candidate)].systemId))
            return static_cast<size_t>(candidate);
    }
    return categoryIndex_;
}

bool Launcher::init(){
#if defined(LAUNCHER_DEVICE)
    // The Miyoo SDL backend defaults to unsynchronized presentation. Request
    // page-flip vsync before SDL initializes the framebuffer; it falls back
    // to adaptive vsync on panels that cannot page flip.
    SDL_SetHint("SDL_MMIYOO_VSYNC_MODE","strict");
#endif
    platform_=platform::detect();
    std::fprintf(stdout,"Platform: %s (%s), SD root %s, screen %dx%d\n",platform_.name.c_str(),platform_.id.c_str(),
                 platform_.sdRoot.c_str(),platform_.screenW,platform_.screenH);
    std::fflush(stdout);
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS)!=0){std::fprintf(stderr,"SDL init: %s\n",SDL_GetError());return false;}
#if !defined(LAUNCHER_DEVICE)
    SDL_SetHint(SDL_HINT_RENDER_DRIVER,"software");
#endif
#if defined(LAUNCHER_DEVICE)
    const Uint32 windowFlags=SDL_WINDOW_FULLSCREEN;
#else
    const Uint32 windowFlags=SDL_WINDOW_RESIZABLE;
#endif
    // The layout is drawn on the 752x560 design canvas and SDL scales it to
    // the window. The V4 and Mini Flip keep the tested 752x560 window; the
    // Mini Plus (640x480) and other handhelds open at their native panel size.
    int windowW=kDesignW, windowH=kDesignH;
    if(platform_.id!="miyoo-mini" && platform_.screenW>0 && platform_.screenH>0){
        windowW=platform_.screenW;
        windowH=platform_.screenH;
    }
    window_=SDL_CreateWindow("CartOS",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,windowW,windowH,windowFlags);
    if(!window_){std::fprintf(stderr,"window: %s\n",SDL_GetError());return false;}
#if defined(LAUNCHER_DEVICE)
    renderer_=SDL_CreateRenderer(window_,-1,SDL_RENDERER_PRESENTVSYNC);
    if(renderer_){
        rendererVsync_=true;
    }else{
        std::fprintf(stderr,"vsync renderer unavailable (%s); using unsynchronized fallback\n",SDL_GetError());
        renderer_=SDL_CreateRenderer(window_,-1,0);
    }
#else
    renderer_=SDL_CreateRenderer(window_,-1,SDL_RENDERER_SOFTWARE);
#endif
    if(!renderer_){std::fprintf(stderr,"renderer: %s\n",SDL_GetError());return false;}
    SDL_RendererInfo rendererInfo{};
    if(SDL_GetRendererInfo(renderer_,&rendererInfo)==0) {
        std::fprintf(stdout,"Renderer: %s (flags=0x%x)\n",rendererInfo.name?rendererInfo.name:"unknown",rendererInfo.flags);
        std::fflush(stdout);
    }
    std::fprintf(stdout,"Input devices: %d joystick(s)\n",SDL_NumJoysticks());
    for(int i=0;i<SDL_NumJoysticks();++i){
        SDL_Joystick* probe=SDL_JoystickOpen(i);
        if(probe){
            std::fprintf(stdout,"Joystick %d: %s, %d button(s), %d hat(s), game-controller=%s\n",
                         i,SDL_JoystickName(probe)?SDL_JoystickName(probe):"unknown",
                         SDL_JoystickNumButtons(probe),SDL_JoystickNumHats(probe),
                         SDL_IsGameController(i)?"yes":"no");
            SDL_JoystickClose(probe);
        }
    }
    SDL_RenderSetLogicalSize(renderer_,kDesignW,kDesignH);
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
    const char* configuredFont=std::getenv("CARTOS_FONT");
    std::string fontPath=(configuredFont && *configuredFont)?configuredFont:
                         (access("assets/font_atlas.png",R_OK)==0?"assets/font_atlas.png":"res/font_atlas.png");
    if(!draw::initializeFont(renderer_,fontPath)){
        std::fprintf(stderr,"font atlas could not be loaded: %s\n",fontPath.c_str());
        return false;
    }
    discoverGames();
    reloadBackgroundChoices();
    const char* skipStartupAnimation=std::getenv("CARTOS_SKIP_STARTUP_ANIMATION");
    startupAnimation_=startupAnimationEnabled_ &&
        !(skipStartupAnimation && std::string(skipStartupAnimation)=="1") &&
        !categories_.empty() && !categories_.front().entries.empty();
    startupAnimationElapsed_=0.0f;
    for(int i=0;i<SDL_NumJoysticks();++i){
        if(SDL_IsGameController(i)) controller_=SDL_GameControllerOpen(i);
        if(controller_) break;
        if(!joystick_) joystick_=SDL_JoystickOpen(i);
    }
    if(controller_){
        char* mapping=SDL_GameControllerMapping(controller_);
        if(mapping){
            std::fprintf(stdout,"Controller mapping: %s\n",mapping);
            std::fflush(stdout);
            SDL_free(mapping);
        }
    }
    return true;
}

void Launcher::discoverGames(){
    const std::string sdRoot=platform_.sdRoot;
    sdRoot_=sdRoot;
    systems_.clear();
    categories_.clear();
    artworkDirectoryCache_.clear();
    discoverSystems(sdRoot);
    loadSettings();
    int variant=0;
    discoverRecent(sdRoot);
    std::unordered_set<std::string> systemsWithGames;
    for(const SystemInfo& system:systems_){
        Category category;
        category.label=system.label;
        category.systemId=system.id;
        std::unordered_set<std::string> artworkFiles;
        const std::string artworkDir=system.artworkPath;
        DIR* artworkHandle=opendir(artworkDir.c_str());
        if(artworkHandle){
            while(dirent* artworkEntry=readdir(artworkHandle)){
                const std::string artworkName=artworkEntry->d_name;
                if(artworkName.size()>4 && lower(artworkName.substr(artworkName.size()-4))==".png")
                    artworkFiles.insert(artworkName);
            }
            closedir(artworkHandle);
        }
        std::function<void(const std::string&,int)> scanDirectory;
        scanDirectory=[&](const std::string& dirPath,int depth){
            if(depth>8) return;
            DIR* dir=opendir(dirPath.c_str());
            if(!dir) return;
            while(dirent* entry=readdir(dir)){
                const std::string filename=entry->d_name;
                if(filename.empty() || filename[0]=='.' || filename=="Imgs") continue;
                const std::string path=dirPath+"/"+filename;
                struct stat pathInfo{};
                const bool isDirectory=entry->d_type==DT_DIR ||
                    (entry->d_type==DT_UNKNOWN && stat(path.c_str(),&pathInfo)==0 && S_ISDIR(pathInfo.st_mode));
                if(isDirectory){
                    scanDirectory(path,depth+1);
                    continue;
                }
                if(entry->d_type!=DT_REG && !regularFile(path)) continue;
                if(!supportedRom(filename,system)) continue;
                const std::string title=filename.substr(0,filename.find_last_of('.'));
                if(isUtilityEntry(title,filename)) continue;
                Game game;
                game.title=title;
                game.system=system.label;
                game.console=system.id;
                game.romPath=path;
                const std::string imageName=title+".png";
                if(artworkFiles.count(imageName)) game.defaultArtPath=artworkDir+"/"+imageName;
                else {
                    const std::string siblingImage=dirPath+"/Imgs/"+imageName;
                    if(regularFile(siblingImage)) game.defaultArtPath=siblingImage;
                }
                // Populate every category through the same artwork loader
                // after Recent and saved lists have also been discovered.
                game.artPath=game.defaultArtPath;
                game.launcherPath=system.launcherPath;
                game.variant=variant++;
                category.entries.push_back(std::move(game));
            }
            closedir(dir);
        };
        for(const std::string& romRoot:system.romPaths)
            scanDirectory(romRoot,0);
        std::sort(category.entries.begin(),category.entries.end(),[](const Game& a,const Game& b){return lower(a.title)<lower(b.title);});
        // OnionOS can contain dozens of system configs. Keep the browser
        // focused on systems with at least one ROM actually present.
        if(!category.entries.empty()){
            systemsWithGames.insert(system.id);
            categories_.push_back(std::move(category));
        }
    }
    systems_.erase(std::remove_if(systems_.begin(),systems_.end(),[&](const SystemInfo& system){
        return systemsWithGames.find(system.id)==systemsWithGames.end();
    }),systems_.end());
    discoverCollections(sdRoot);
    discoverApps(sdRoot);
    rememberedSelections_.assign(categories_.size(),0);
    std::size_t total=0;
    for(const Category& category:categories_) total+=category.entries.size();
    std::fprintf(stdout,"Discovered %zu entries across %zu lists under %s/Roms\n",total,categories_.size(),sdRoot.c_str());
    std::fflush(stdout);
}

void Launcher::discoverSystems(const std::string& sdRoot){
    const std::string emuRoot=sdRoot+"/Emu";
    DIR* directory=opendir(emuRoot.c_str());
    if(!directory) return;
    while(dirent* entry=readdir(directory)){
        const std::string id=entry->d_name;
        if(id.empty() || id[0]=='.' || id=="_COLLECTIONS") continue;
        const std::string systemDir=emuRoot+"/"+id;
        const std::string config=readText(systemDir+"/config.json");
        if(config.empty()) continue;
        const std::string label=jsonString(config,"label");
        if(isUtilityEntry(label,id)) continue;
        const std::string launch=jsonString(config,"launch");
        const std::string romPaths=jsonString(config,"rompath");
        const std::string extlist=jsonString(config,"extlist");
        if(label.empty() || launch.empty() || romPaths.empty() || extlist.empty()) continue;

        SystemInfo system;
        system.id=id;
        system.label=label;
        system.launcherPath=resolveConfigPath(launch,sdRoot,systemDir);
        if(!regularFile(system.launcherPath)) continue;
        for(const std::string& extension:splitPipeList(extlist)){
            const std::string normalized=extension[0]=='.'?lower(extension):"."+lower(extension);
            system.extensions.push_back(normalized);
        }
        if(system.extensions.empty()) continue;
        for(const std::string& romPath:splitPipeList(romPaths)){
            // OnionOS paths are commonly relative to Emu/<system> (for
            // example ../../Roms/SCUMMVM/Shortcuts), not to the Roms root.
            std::string resolved=resolveConfigPath(romPath,sdRoot,systemDir);
            if(access(resolved.c_str(),R_OK)==0) system.romPaths.push_back(resolved);
        }
        if(system.romPaths.empty()) continue;
        system.artworkPath=resolveConfigPath(jsonString(config,"imgpath"),sdRoot,systemDir);
        if(system.artworkPath.empty()) system.artworkPath=system.romPaths.front()+"/Imgs";
        systems_.push_back(std::move(system));
    }
    closedir(directory);
    std::sort(systems_.begin(),systems_.end(),[](const SystemInfo& a,const SystemInfo& b){return lower(a.label)<lower(b.label);});
}

const SystemInfo* Launcher::systemForLauncher(const std::string& launcher) const{
    const std::string normalized=comparableRomPath(launcher,sdRoot_);
    for(const SystemInfo& system:systems_){
        if(normalized==comparableRomPath(system.launcherPath,sdRoot_)) return &system;
    }
    return nullptr;
}

const SystemInfo* Launcher::systemForRomPath(const std::string& romPath) const{
    const std::string normalized=comparableRomPath(romPath,sdRoot_);
    for(const SystemInfo& system:systems_){
        for(const std::string& root:system.romPaths){
            const std::string prefix=comparableRomPath(root,sdRoot_);
            if(normalized.compare(0,prefix.size(),prefix)==0 &&
               (normalized.size()==prefix.size() || normalized[prefix.size()]=='/')) return &system;
        }
    }
    return nullptr;
}

int Launcher::cartridgeScaleFor(const std::string& systemId) const{
    const auto found=cartridgeScaleBySystem_.find(systemId);
    return found==cartridgeScaleBySystem_.end()?1:found->second;
}

bool Launcher::systemHiddenFor(const std::string& systemId) const{
    const auto found=hiddenSystemById_.find(systemId);
    return found!=hiddenSystemById_.end() && found->second;
}

void Launcher::discoverRecent(const std::string& sdRoot){
    Category recent;
    recent.label="recent";
    std::vector<std::string> seenPaths;

    auto addRecent=[&](const std::string& rawPath,const std::string& rawTitle,const SystemInfo* hintedSystem){
        if(rawPath.empty()) return;
        const std::string romPath=sdPath(rawPath,sdRoot);
        if(std::find(seenPaths.begin(),seenPaths.end(),romPath)!=seenPaths.end() || !regularFile(romPath)) return;
        const SystemInfo* system=hintedSystem?hintedSystem:systemForRomPath(romPath);
        const std::string filename=romPath.substr(romPath.find_last_of('/')==std::string::npos?0:romPath.find_last_of('/')+1);
        if(!system || !supportedRom(filename,*system) || isUtilityEntry(rawTitle,filename)) return;
        seenPaths.push_back(romPath);
        Game game;
        game.title=rawTitle;
        if(game.title.empty()) game.title=filename.substr(0,filename.find_last_of('.'));
        game.system=system->label;
        game.console=system->id;
        game.romPath=romPath;
        game.launcherPath=system->launcherPath;
        const std::string imagePath=system->artworkPath+"/"+filename.substr(0,filename.find_last_of('.'))+".png";
        if(regularFile(imagePath)) game.artPath=imagePath;
        game.variant=static_cast<int>(recent.entries.size());
        recent.entries.push_back(std::move(game));
    };

    // RetroArch maintains this history itself. We only read it; the emulator
    // remains responsible for its normal history and save updates.
    std::ifstream history(sdRoot+"/Saves/CurrentProfile/lists/content_history.lpl");
    std::string line;
    while(std::getline(history,line)){
        const std::string rawPath=jsonString(line,"path");
        if(rawPath.empty()) continue;
        const std::string rawTitle=jsonString(line,"label");
        addRecent(rawPath,rawTitle,nullptr);
    }

    // If RetroArch history is unavailable or empty, use Onion's recent list as
    // a read-only fallback, excluding apps and non-ROM shortcuts.
    if(recent.entries.empty()){
        std::ifstream onionRecent(sdRoot+"/Roms/recentlist.json");
        while(std::getline(onionRecent,line)){
            const std::string rawPath=jsonString(line,"rompath");
            const std::string launcher=jsonString(line,"launch");
            const SystemInfo* system=systemForRomPath(sdPath(rawPath,sdRoot));
            if(!system) system=systemForLauncher(launcher);
            if(system) addRecent(rawPath,jsonString(line,"label"),system);
        }
    }
    categories_.push_back(std::move(recent));
}

void Launcher::discoverCollections(const std::string& sdRoot){
    const std::string collectionDir=sdRoot+"/Emu/_COLLECTIONS/COLLECTIONS";
    DIR* dir=opendir(collectionDir.c_str());
    if(!dir) return;

    auto loadList=[&](const std::string& filePath,const std::string& label,bool keepEmpty){
        if(isUtilityEntry(label,"")) return;
        Category category;
        category.label=label;
        std::ifstream file(filePath.c_str());
        std::string line;
        int variant=0;
        while(std::getline(file,line)){
            const std::string romRaw=jsonString(line,"rompath");
            const std::string launcherRaw=jsonString(line,"launch");
            const std::string title=jsonString(line,"label");
            if(romRaw.empty() || title.empty() || launcherRaw.find("/_COLLECTIONS/")!=std::string::npos) continue;
            const std::string romPath=sdPath(romRaw,sdRoot);
            const SystemInfo* system=systemForRomPath(romPath);
            if(!system) system=systemForLauncher(launcherRaw);
            if(!system) continue;
            const std::string filename=romPath.substr(romPath.find_last_of('/')==std::string::npos?0:romPath.find_last_of('/')+1);
            if(!supportedRom(filename,*system) || !regularFile(romPath) || isUtilityEntry(title,filename)) continue;
            Game game;
            game.title=title;
            game.system=system->label;
            game.console=system->id;
            game.romPath=romPath;
            game.launcherPath=sdPath(launcherRaw,sdRoot);
            const std::string imageRaw=jsonString(line,"imgpath");
            if(!imageRaw.empty()) game.artPath=sdPath(imageRaw,sdRoot);
            if(game.artPath.empty() || access(game.artPath.c_str(),R_OK)!=0){
                const std::string titleNoExt=filename.substr(0,filename.find_last_of('.'));
                game.artPath=romPath.substr(0,romPath.find_last_of('/')+1)+"Imgs/"+titleNoExt+".png";
                if(access(game.artPath.c_str(),R_OK)!=0) game.artPath.clear();
            }
            game.variant=variant++;
            category.entries.push_back(std::move(game));
        }
        if(keepEmpty || !category.entries.empty()){
            std::sort(category.entries.begin(),category.entries.end(),[](const Game& a,const Game& b){return lower(a.title)<lower(b.title);});
            categories_.push_back(std::move(category));
        }
    };

    // Onion stores the active Favourite list as favourite.json, while these
    // manifests preserve each named custom list independently.
    loadList(sdRoot+"/Roms/favourite.json","FAVOURITE",false);
    std::vector<std::pair<std::string,std::string>> files;
    while(dirent* entry=readdir(dir)){
        const std::string name=entry->d_name;
        if(name.empty() || name[0]=='.' || name[0]=='-' || name.size()<6 || lower(name.substr(name.size()-5))!=".json") continue;
        files.push_back({name.substr(0,name.size()-5),collectionDir+"/"+name});
    }
    closedir(dir);
    std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return lower(a.first)<lower(b.first);});
    for(const auto& file:files) loadList(file.second,file.first,true);
}

void Launcher::discoverApps(const std::string& sdRoot){
    const std::string appRoot=sdRoot+"/App";
    DIR* dir=opendir(appRoot.c_str());
    Category apps;
    apps.label="APPS";
    int variant=0;
    if(dir){
    while(dirent* entry=readdir(dir)){
        const std::string folder=entry->d_name;
        if(folder.empty() || folder[0]=='.' || folder=="CartOS") continue;
        const std::string appDir=appRoot+"/"+folder;
        const std::string configPath=appDir+"/config.json";
        if(!regularFile(configPath)) continue;
        const std::string config=readText(configPath);
        const std::string label=jsonString(config,"label");
        std::string launch=jsonString(config,"launch");
        // Older Onion app packages may omit the launch key; their conventional
        // entry point is launch.sh. Newer packages are followed exactly as
        // declared in config.json.
        if(launch.empty()) launch="launch.sh";
        const std::string launchPath=resolveConfigPath(launch,sdRoot,appDir);
        if(label.empty() || isUtilityEntry(label,folder) || !regularFile(launchPath) || access(launchPath.c_str(),R_OK)!=0) continue;
        Game app;
        app.kind=EntryKind::App;
        app.title=label;
        app.system="ONION APP";
        app.launcherPath=launchPath;
        app.workingDirectory=appDir;
        const std::string icon=jsonString(config,"icon");
        if(!icon.empty()){
            app.artPath=resolveConfigPath(icon,sdRoot,appDir);
            if(!regularFile(app.artPath) || access(app.artPath.c_str(),R_OK)!=0) app.artPath.clear();
        }
        app.variant=variant++;
        apps.entries.push_back(std::move(app));
    }
    closedir(dir);
    }
    std::sort(apps.entries.begin(),apps.entries.end(),[](const Game& a,const Game& b){return lower(a.title)<lower(b.title);});
    Game mainUi;
    mainUi.kind=EntryKind::App;
    mainUi.title="Main UI";
    mainUi.system="ONION SYSTEM";
    mainUi.launcherPath="builtin:onion-main-ui";
    mainUi.artPath=sdRoot+"/App/CartOS/res/onionos_logo.png";
    if(access(mainUi.artPath.c_str(),R_OK)!=0)
        mainUi.artPath=access("assets/onionos_logo.png",R_OK)==0?"assets/onionos_logo.png":"";
    mainUi.onionMainUi=true;
    mainUi.variant=variant++;
    apps.entries.push_back(std::move(mainUi));
    Game settings;
    settings.kind=EntryKind::App;
    settings.title="CartOS settings";
    settings.system="CARTOS APP";
    settings.launcherPath="builtin:cartos-settings";
    settings.artPath=sdRoot+"/App/CartOS/res/icon/cartos.png";
    if(access(settings.artPath.c_str(),R_OK)!=0)
        settings.artPath=access("res/icon/cartos.png",R_OK)==0?"res/icon/cartos.png":"";
    settings.cartLauncherSettings=true;
    settings.variant=variant++;
    apps.entries.push_back(std::move(settings));
    std::sort(apps.entries.begin(),apps.entries.end(),[](const Game& a,const Game& b){return lower(a.title)<lower(b.title);});
    categories_.push_back(std::move(apps));
}

void Launcher::shutdown(){
    if(backgroundArtwork_){SDL_DestroyTexture(backgroundArtwork_);backgroundArtwork_=nullptr;}
    for(Category& category:categories_) clearArtwork(category.entries);
    draw::shutdownFont();
    if(controller_){SDL_GameControllerClose(controller_);controller_=nullptr;}
    if(joystick_){SDL_JoystickClose(joystick_);joystick_=nullptr;}
    if(renderer_){SDL_DestroyRenderer(renderer_);renderer_=nullptr;}
    if(window_){SDL_DestroyWindow(window_);window_=nullptr;}
    SDL_Quit();
#if defined(LAUNCHER_DEVICE)
    // The Miyoo framebuffer is double-height. SDL page flips can leave scanout
    // on the lower page (yoffset=480); OnionOS apps expect the top page.
    resetFramebufferToTopPage();
#endif
}

void Launcher::renderBackground(){
    SDL_SetRenderDrawColor(renderer_,0,0,0,255);
    SDL_RenderClear(renderer_);
    draw::background(renderer_,backgroundArtwork_,kDesignW,kDesignH);
}

void Launcher::move(int direction){
    Category* category=currentCategory();
    if(category && !category->entries.empty()){
        target_+=static_cast<float>(direction);
        if(!wrapGameBrowsing_)
            target_=std::max(0.0f,std::min(target_,static_cast<float>(category->entries.size()-1)));
    }
}

void Launcher::moveCategory(int direction){
    if(categories_.empty()) return;
    if(Category* category=currentCategory()) clearArtwork(category->entries);
    categoryIndex_=adjacentVisibleCategory(direction);
    const int selection=rememberedSelections_[categoryIndex_];
    position_=target_=static_cast<float>(selection);
}

void Launcher::setScrollHeld(int direction,bool held){
    if(direction<0 || direction>=static_cast<int>(scrollHeld_.size())) return;
    const size_t index=static_cast<size_t>(direction);
    if(held){
        if(scrollHeld_[index]) return;
        scrollHeld_[index]=true;
        scrollSettingsMode_[index]=(overlay_==Overlay::Settings);
        scrollRepeatAt_[index]=SDL_GetTicks64()+kScrollInitialDelayMs;
        if(scrollSettingsMode_[index]){
            if(direction==2) moveSettingsSelection(-1);
            else if(direction==3) moveSettingsSelection(1);
        }else{
            if(direction==0) move(-1);
            else if(direction==1) move(1);
            else if(direction==2) moveCategory(-1);
            else moveCategory(1);
        }
    }else{
        scrollHeld_[index]=false;
    }
}

void Launcher::updateHeldScroll(Uint64 now){
    if(launchAnimation_) return;
    for(size_t index=0;index<scrollHeld_.size();++index){
        if(!scrollHeld_[index] || now<scrollRepeatAt_[index]) continue;
        if(scrollSettingsMode_[index]){
            if(overlay_!=Overlay::Settings || index<2) continue;
            moveSettingsSelection(index==2?-1:1);
        }else{
            if(overlay_!=Overlay::None) continue;
            if(index==0) move(-1);
            else if(index==1) move(1);
            else if(index==2) moveCategory(-1);
            else moveCategory(1);
        }
        // Schedule from the current frame instead of catching up missed ticks,
        // which keeps scrolling bounded even if rendering briefly stalls.
        scrollRepeatAt_[index]=now+kScrollRepeatIntervalMs;
    }
}

void Launcher::launchSelected(){
    Category* category=currentCategory();
    if(!category || category->entries.empty()) return;
    const int selected=wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(category->entries.size()));
    Game& entry=category->entries[static_cast<size_t>(selected)];
    if(entry.cartLauncherSettings){
        settingsSelection_=0;
        settingsScrollOffset_=0;
        overlay_=Overlay::Settings;
        return;
    }
    if(entry.kind==EntryKind::Game){
        beginLaunchAnimation(categoryIndex_,static_cast<size_t>(selected));
        return;
    }
    launchGame(entry);
}

void Launcher::beginLaunchAnimation(size_t categoryIndex,size_t entryIndex){
    if(categoryIndex>=categories_.size() || entryIndex>=categories_[categoryIndex].entries.size()) return;
    launchAnimationCategory_=categoryIndex;
    launchAnimationEntry_=entryIndex;
    previewShowingDetails_=false;
    resetPreviewArtwork();
    launchAnimationElapsed_=0.0f;
    launchCenterDuration_=overlay_==Overlay::Search?0.0f:kLaunchCenterDuration;
    launchCenteringOffset_=0.0f;
    launchCenteringPosition_=std::round(position_);
    launchReverseElapsed_=0.0f;
    launchReverseStartScale_=1.34f;
    if(launchCenterDuration_>0.0f){
        const float selected=launchCenteringPosition_;
        const float spacing=carouselSpacing_==0?215.0f:(carouselSpacing_==2?255.0f:240.0f);
        // Bring the already-selected cartridge to the center before growing it.
        // This matters when A is pressed while the carousel is still settling
        // between entries: position_ may be fractional, so the selected cart is
        // drawn slightly to one side in the normal browser view.
        launchCenteringOffset_=(position_-selected)*spacing;
    }
    launchAwaitingConfirm_=false;
    launchArtworkLoading_=false;
    launchReversing_=false;
    launchStepTwoConfirmed_=false;
    launchFirstPressAt_=SDL_GetTicks64();
    launchAnimation_=true;
}

Game* Launcher::launchingGame(){
    if(launchAnimationCategory_>=categories_.size() ||
       launchAnimationEntry_>=categories_[launchAnimationCategory_].entries.size()) return nullptr;
    return &categories_[launchAnimationCategory_].entries[launchAnimationEntry_];
}

void Launcher::loadArtworkVariants(Game& game){
    if(game.kind!=EntryKind::Game) return;
    if(game.artVariants.empty()){
        const std::string primaryImage=!game.defaultArtPath.empty()?game.defaultArtPath:game.artPath;
        if(!primaryImage.empty() && regularFile(primaryImage))
            game.artVariants.push_back(primaryImage);
        if(!game.artPath.empty() && regularFile(game.artPath) &&
           std::find(game.artVariants.begin(),game.artVariants.end(),game.artPath)==game.artVariants.end())
            game.artVariants.push_back(game.artPath);
        const SystemInfo* system=systemForRomPath(game.romPath);
        if(system){
            if(game.artVariants.empty()){
                const std::string conventional=system->artworkPath+"/"+filenameWithoutExtension(game.romPath)+".png";
                if(regularFile(conventional)) game.artVariants.push_back(conventional);
            }
            std::string relativeRomPath;
            // OnionOS and RetroArch histories may store the same ROM through
            // paths containing a different SD root or `..` components.
            // Normalize both sides before deriving the artwork subfolder.
            const std::string normalizedGamePath=comparableRomPath(game.romPath,sdRoot_);
            size_t rootIndex=0;
            for(size_t i=0;i<system->romPaths.size();++i){
                const std::string& romRoot=system->romPaths[i];
                const std::string normalizedRoot=comparableRomPath(romRoot,sdRoot_);
                if(normalizedGamePath.compare(0,normalizedRoot.size(),normalizedRoot)==0 &&
                   normalizedGamePath.size()>normalizedRoot.size() && normalizedGamePath[normalizedRoot.size()]=='/'){
                    relativeRomPath=normalizedGamePath.substr(normalizedRoot.size()+1);
                    rootIndex=i;
                    break;
                }
            }
            if(!relativeRomPath.empty()){
                if(system->romPaths.size()>1){
                    const std::string& romRoot=system->romPaths[rootIndex];
                    const size_t rootSlash=romRoot.find_last_of('/');
                    const std::string rootLabel=rootSlash==std::string::npos?romRoot:romRoot.substr(rootSlash+1);
                    relativeRomPath=rootLabel+"/"+relativeRomPath;
                }
                const size_t romSlash=relativeRomPath.find_last_of('/');
                const std::string relativeDirectory=romSlash==std::string::npos?std::string():relativeRomPath.substr(0,romSlash);
                const std::string romFilename=romSlash==std::string::npos?relativeRomPath:relativeRomPath.substr(romSlash+1);
                const std::string romTitle=filenameWithoutExtension(romFilename);
                const std::string variantsDirectory=system->artworkPath+"/CartOS"+
                    (relativeDirectory.empty()?std::string():"/"+relativeDirectory);
                const std::string legacyDirectory=system->artworkPath+"/CartOS/"+relativeRomPath;
                std::vector<std::string> additionalImages;
                auto scanArtworkDirectory=[&](const std::string& directory,bool flatNames){
                    auto cached=artworkDirectoryCache_.find(directory);
                    if(cached==artworkDirectoryCache_.end()){
                        std::vector<std::string> imageNames;
                        DIR* handle=opendir(directory.c_str());
                        if(handle){
                            while(dirent* entry=readdir(handle)){
                                const std::string name=entry->d_name;
                                if(name.empty() || name[0]=='.' || name.size()<5 ||
                                   lower(name.substr(name.size()-4))!=".png") continue;
                                if(regularFile(directory+"/"+name)) imageNames.push_back(name);
                            }
                            closedir(handle);
                        }
                        std::sort(imageNames.begin(),imageNames.end(),[](const std::string& a,const std::string& b){
                            return lower(a)<lower(b);
                        });
                        cached=artworkDirectoryCache_.emplace(directory,std::move(imageNames)).first;
                    }
                    const std::string expectedPrefix=lower(romTitle+" - ");
                    const std::string expectedSingleImage=lower(romTitle+".png");
                    auto appendImage=[&](const std::string& name){
                        const std::string image=directory+"/"+name;
                        if(std::find(game.artVariants.begin(),game.artVariants.end(),image)==game.artVariants.end() &&
                           std::find(additionalImages.begin(),additionalImages.end(),image)==additionalImages.end())
                            additionalImages.push_back(image);
                    };
                    if(flatNames){
                        // The CartOS artwork directory can contain hundreds or
                        // thousands of images. Use the sorted cache to find
                        // this ROM's names directly instead of rescanning the
                        // entire directory for every game during startup.
                        auto exact=std::lower_bound(cached->second.begin(),cached->second.end(),expectedSingleImage,
                            [](const std::string& name,const std::string& value){return lower(name)<value;});
                        for(;exact!=cached->second.end() && lower(*exact)==expectedSingleImage;++exact)
                            appendImage(*exact);
                        auto view=std::lower_bound(cached->second.begin(),cached->second.end(),expectedPrefix,
                            [](const std::string& name,const std::string& value){return lower(name)<value;});
                        for(;view!=cached->second.end();++view){
                            const std::string comparableName=lower(*view);
                            if(comparableName.compare(0,expectedPrefix.size(),expectedPrefix)!=0) break;
                            if(comparableName.size()>expectedPrefix.size()+4)
                                appendImage(*view);
                        }
                    }else{
                        for(const std::string& name:cached->second) appendImage(name);
                    }
                };
                // Flat names make it quick to add many views for a game, while
                // still accepting the earlier per-ROM directory layout.
                scanArtworkDirectory(variantsDirectory,true);
                scanArtworkDirectory(legacyDirectory,false);
                std::sort(additionalImages.begin(),additionalImages.end(),[&romTitle](const std::string& a,const std::string& b){
                    const std::string aName=lower(a.substr(a.find_last_of('/')+1));
                    const std::string bName=lower(b.substr(b.find_last_of('/')+1));
                    const bool aSingleImage=aName==lower(romTitle+".png");
                    const bool bSingleImage=bName==lower(romTitle+".png");
                    if(aSingleImage!=bSingleImage) return aSingleImage;
                    return aName==bName?lower(a)<lower(b):aName<bName;
                });
                for(const std::string& image:additionalImages){
                    if(game.artVariants.size()>=20) break;
                    game.artVariants.push_back(image);
                }
            }
        }
        if(!game.artVariants.empty() && game.defaultArtPath.empty()) game.defaultArtPath=game.artVariants.front();
    }
    if(game.artVariants.empty()) return;
    if(game.artSelection>=game.artVariants.size()) game.artSelection=0;
    game.artPath=game.artVariants[game.artSelection];
}

void Launcher::selectPreviewArtwork(size_t selection){
    Game* game=launchingGame();
    if(!game || game->artVariants.empty()) return;
    game->artSelection=std::min(selection,game->artVariants.size()-1);
    const std::string& image=game->artVariants[game->artSelection];
    if(game->artPath==image) return;
    game->artPath=image;
    if(game->artwork){SDL_DestroyTexture(game->artwork);game->artwork=nullptr;}
    game->artworkAttempted=false;
}

void Launcher::browsePreviewPage(int direction){
    Game* game=launchingGame();
    if(!game) return;
    const size_t imageCount=game->artVariants.size();
    if(previewShowingDetails_){
        previewShowingDetails_=false;
        if(imageCount>0)
            selectPreviewArtwork(direction>0?0:imageCount-1);
        return;
    }
    if(imageCount==0){
        previewShowingDetails_=true;
        return;
    }
    if(direction>0 && game->artSelection+1<imageCount){
        selectPreviewArtwork(game->artSelection+1);
    }else if(direction<0 && game->artSelection>0){
        selectPreviewArtwork(game->artSelection-1);
    }else{
        previewShowingDetails_=true;
    }
}

void Launcher::resetPreviewArtwork(){
    Game* game=launchingGame();
    previewShowingDetails_=false;
    if(!game || game->artVariants.empty()) return;
    selectPreviewArtwork(0);
}

void Launcher::drawPreviewGameDetails(const Game& game){
    const SDL_Rect panel{76,116,600,328};
    fillRect(renderer_,panel,{4,7,15,248});
    outlineRect(renderer_,panel,{70,82,100,255});
    const SDL_Color white{238,241,247,255};
    const SDL_Color muted{150,158,171,255};
    const std::string heading="GAME DETAILS";
    draw::text(renderer_,(kDesignW-draw::textWidth(heading,25))/2,132,heading,25,white);
    draw::text(renderer_,100,178,"TITLE",15,muted);
    draw::text(renderer_,100,198,shortLabel(game.title,48),20,white);
    draw::text(renderer_,100,236,"SYSTEM",15,muted);
    draw::text(renderer_,100,256,game.system,20,white);
    draw::text(renderer_,100,294,"PLAY TIME",15,muted);
    const bool activityTrackerInstalled=access((sdRoot_+"/App/PlayActivity/launch.sh").c_str(),R_OK)==0;
    const long long playedSeconds=activityTrackerInstalled?playtimeSecondsFor(game):-1;
    std::string playtime=activityTrackerInstalled?"No playtime":"Install Activity Tracker to see playtime";
    if(activityTrackerInstalled && playedSeconds>=3600)
        playtime=std::to_string(playedSeconds/3600)+"h "+std::to_string((playedSeconds%3600)/60)+"m";
    else if(activityTrackerInstalled && playedSeconds>=60)
        playtime=std::to_string(playedSeconds/60)+"m";
    else if(activityTrackerInstalled && playedSeconds>=0)
        playtime=std::to_string(playedSeconds)+"s";
    const int playtimeSize=activityTrackerInstalled?20:16;
    draw::text(renderer_,100,314,shortLabel(playtime,56),playtimeSize,activityTrackerInstalled?white:muted);
    draw::text(renderer_,100,352,"ROM FILE",15,muted);
    draw::text(renderer_,100,372,shortLabel(game.romPath,62),16,white);
}

void Launcher::loadSettings(){
    const std::string data=readText(sdRoot_+"/App/CartOS/settings.ini");
    std::istringstream lines(data);
    std::string line;
    int legacyScale=-1;
    while(std::getline(lines,line)){
        const size_t separator=line.find('=');
        if(separator==std::string::npos) continue;
        const std::string key=line.substr(0,separator), value=line.substr(separator+1);
        if(key=="startup_animation") startupAnimationEnabled_=(value!="0");
        else if(key=="show_game_count") showGameCount_=(value!="0");
        else if(key=="show_battery_level") showBatteryLevel_=(value!="0");
        else if(key=="show_control_hints") showControlHints_=(value!="0");
        else if(key=="invert_text_colour") invertTextColour_=(value=="1");
        else if(key=="hide_game_title") hideGameTitle_=(value=="1");
        else if(key=="background_file") backgroundFile_=value;
        else if(key=="cartridge_scale") legacyScale=std::max(0,std::min(2,std::atoi(value.c_str())));
        else if(key.compare(0,16,"cartridge_scale_")==0){
            const std::string id=key.substr(16);
            if(!id.empty()){
                cartridgeScaleBySystem_[id]=std::max(0,std::min(2,std::atoi(value.c_str())));
            }
        }
        else if(key.compare(0,14,"hidden_system_")==0){
            const std::string id=key.substr(14);
            if(!id.empty()) hiddenSystemById_[id]=(value=="1");
        }
        else if(key=="carousel_spacing") carouselSpacing_=std::max(0,std::min(2,std::atoi(value.c_str())));
        else if(key=="animation_speed") animationSpeed_=std::max(0,std::min(2,std::atoi(value.c_str())));
        else if(key=="title_size") titleSize_=std::max(0,std::min(2,std::atoi(value.c_str())));
        else if(key=="wrap_game_browsing") wrapGameBrowsing_=(value!="0");
    }
    for(const SystemInfo& system:systems_){
        if(cartridgeScaleBySystem_.find(system.id)==cartridgeScaleBySystem_.end())
            cartridgeScaleBySystem_[system.id]=(legacyScale>=0?legacyScale:1);
        if(hiddenSystemById_.find(system.id)==hiddenSystemById_.end())
            hiddenSystemById_[system.id]=false;
    }
}

void Launcher::saveSettings() const{
    const std::string path=sdRoot_+"/App/CartOS/settings.ini";
    const std::string temporary=path+".tmp";
    std::ofstream file(temporary.c_str(),std::ios::out|std::ios::trunc);
    if(!file) return;
    file<<"startup_animation="<<(startupAnimationEnabled_?1:0)<<"\n"
        <<"show_game_count="<<(showGameCount_?1:0)<<"\n"
        <<"show_battery_level="<<(showBatteryLevel_?1:0)<<"\n"
        <<"show_control_hints="<<(showControlHints_?1:0)<<"\n"
        <<"invert_text_colour="<<(invertTextColour_?1:0)<<"\n"
        <<"hide_game_title="<<(hideGameTitle_?1:0)<<"\n"
        <<"background_file="<<backgroundFile_<<"\n"
        <<"carousel_spacing="<<carouselSpacing_<<"\n"
        <<"animation_speed="<<animationSpeed_<<"\n"
        <<"title_size="<<titleSize_<<"\n"
        <<"wrap_game_browsing="<<(wrapGameBrowsing_?1:0)<<"\n";
    for(const auto& scale:cartridgeScaleBySystem_)
        file<<"cartridge_scale_"<<scale.first<<"="<<scale.second<<"\n";
    for(const auto& hidden:hiddenSystemById_)
        file<<"hidden_system_"<<hidden.first<<"="<<(hidden.second?1:0)<<"\n";
    file.close();
    if(file) rename(temporary.c_str(),path.c_str());
}

void Launcher::reloadBackgroundChoices(){
    const std::string directory=sdRoot_+"/App/CartOS";
    backgroundChoices_.clear();
    backgroundChoices_.push_back({"","NONE"});

    std::vector<BackgroundOption> named;
    DIR* handle=opendir(directory.c_str());
    if(handle){
        while(const dirent* entry=readdir(handle)){
            const std::string filename=entry->d_name;
            if(filename=="." || filename==".." || filename.compare(0,2,"._")==0) continue;
            const std::string folded=lower(filename);
            if(folded.size()<4 || folded.substr(folded.size()-4)!=".png") continue;
            if(!regularFile(directory+"/"+filename)) continue;
            if(folded=="bg.png") continue;
            if(folded.size()<=7 || folded.compare(0,3,"bg_")!=0) continue;

            std::string label=filename.substr(3,filename.size()-7);
            if(label.size()>=2 && label.front()=='(' && label.back()==')')
                label=label.substr(1,label.size()-2);
            if(label.empty()) continue;
            named.push_back({filename,label});
        }
        closedir(handle);
    }
    std::sort(named.begin(),named.end(),[](const BackgroundOption& a,const BackgroundOption& b){
        return lower(a.label)<lower(b.label);
    });

    const std::string defaultPath=directory+"/BG.png";
    const bool hasDefault=regularFile(defaultPath);
    backgroundChoices_.push_back({"BG.png",hasDefault?"DEFAULT":"DEFAULT (MISSING)"});
    backgroundChoices_.insert(backgroundChoices_.end(),named.begin(),named.end());

    if(!backgroundFile_.empty() && backgroundFile_!="BG.png" &&
       std::none_of(backgroundChoices_.begin(),backgroundChoices_.end(),[&](const BackgroundOption& choice){
           return choice.filename==backgroundFile_;
       })){
        // If a named image was removed, fall back to BG.png when available,
        // otherwise clear the selection. This also removes stale choices after
        // the user refreshes the browser.
        backgroundFile_=hasDefault?"BG.png":"";
    }

    backgroundSelection_=0;
    for(size_t i=0;i<backgroundChoices_.size();++i){
        if(backgroundChoices_[i].filename==backgroundFile_){
            backgroundSelection_=static_cast<int>(i);
            break;
        }
    }

    SDL_Texture* replacement=nullptr;
    if(!backgroundFile_.empty()){
        const std::string path=directory+"/"+backgroundFile_;
        if(regularFile(path)){
            std::string failure;
            replacement=artwork::loadPng(renderer_,path,2048,&failure);
            std::ofstream log(directory+"/background-debug.log",std::ios::out|std::ios::app);
            if(log) log<<"Background choice load: "<<path<<" -> "<<(replacement?"loaded":failure)<<"\n";
        }
    }
    if(backgroundArtwork_) SDL_DestroyTexture(backgroundArtwork_);
    backgroundArtwork_=replacement;
}

bool Launcher::selectBackground(int direction){
    if(backgroundChoices_.empty()) reloadBackgroundChoices();
    if(backgroundChoices_.empty()) return false;
    const int next=wrapIndex(backgroundSelection_+direction,static_cast<int>(backgroundChoices_.size()));
    const BackgroundOption& choice=backgroundChoices_[static_cast<size_t>(next)];
    SDL_Texture* replacement=nullptr;
    if(!choice.filename.empty()){
        const std::string path=sdRoot_+"/App/CartOS/"+choice.filename;
        if(regularFile(path)){
            std::string failure;
            replacement=artwork::loadPng(renderer_,path,2048,&failure);
            if(!replacement){
                statusMessage_="Could not load background: "+choice.label;
                statusUntil_=SDL_GetTicks64()+2200;
                return false;
            }
        }
    }
    if(backgroundArtwork_) SDL_DestroyTexture(backgroundArtwork_);
    backgroundArtwork_=replacement;
    backgroundSelection_=next;
    backgroundFile_=choice.filename;
    statusMessage_=choice.filename.empty()?"Background disabled":"Background: "+choice.label;
    statusUntil_=SDL_GetTicks64()+1600;
    return true;
}

void Launcher::changeSetting(int direction){
    const int systemCount=static_cast<int>(systems_.size());
    bool systemVisibilityChanged=false;
    if(settingsSelection_>=12){
        const int systemSetting=settingsSelection_-12;
        const int systemIndex=systemSetting/2;
        if(systemIndex>=0 && systemIndex<systemCount){
            const std::string& id=systems_[static_cast<size_t>(systemIndex)].id;
            if(systemSetting%2==0)
                cartridgeScaleBySystem_[id]=wrapIndex(cartridgeScaleFor(id)+direction,3);
            else{
                hiddenSystemById_[id]=!systemHiddenFor(id);
                systemVisibilityChanged=true;
            }
        }
    }else{
        switch(settingsSelection_){
            case 0: carouselSpacing_=wrapIndex(carouselSpacing_+direction,3); break;
            case 1: animationSpeed_=wrapIndex(animationSpeed_+direction,3); break;
            case 2: startupAnimationEnabled_=!startupAnimationEnabled_; break;
            case 3: titleSize_=wrapIndex(titleSize_+direction,3); break;
            case 4: showGameCount_=!showGameCount_; break;
            case 5: showBatteryLevel_=!showBatteryLevel_; break;
            case 6: showControlHints_=!showControlHints_; break;
            case 7: wrapGameBrowsing_=!wrapGameBrowsing_; break;
            case 8: invertTextColour_=!invertTextColour_; break;
            case 9: hideGameTitle_=!hideGameTitle_; break;
            case 10: selectBackground(direction); break;
            case 11:
                startupAnimationEnabled_=true;
                for(auto& scale:cartridgeScaleBySystem_) scale.second=1;
                for(const SystemInfo& system:systems_) cartridgeScaleBySystem_[system.id]=1;
                for(auto& hidden:hiddenSystemById_) hidden.second=false;
                for(const SystemInfo& system:systems_) hiddenSystemById_[system.id]=false;
                animationSpeed_=1; carouselSpacing_=1; titleSize_=1;
                showGameCount_=true; showBatteryLevel_=true; showControlHints_=true;
                invertTextColour_=false; hideGameTitle_=false;
                backgroundFile_="BG.png";
                wrapGameBrowsing_=true;
                reloadBackgroundChoices();
                statusMessage_="Settings reset";
                statusUntil_=SDL_GetTicks64()+1800;
                break;
        }
    }
    saveSettings();
    if(systemVisibilityChanged){
        const bool hidden=systemHiddenFor(systems_[static_cast<size_t>((settingsSelection_-12)/2)].id);
        statusMessage_=hidden?"System hidden":"System shown";
        statusUntil_=SDL_GetTicks64()+1800;
    }
}

void Launcher::moveSettingsSelection(int direction){
    settingsSelection_=wrapIndex(settingsSelection_+direction,settingsRowCount());
    constexpr int visibleRows=8;
    if(settingsSelection_<settingsScrollOffset_)
        settingsScrollOffset_=settingsSelection_;
    else if(settingsSelection_>=settingsScrollOffset_+visibleRows)
        settingsScrollOffset_=settingsSelection_-visibleRows+1;
}

int Launcher::settingsRowCount() const{
    return static_cast<int>(systems_.size())*2+12;
}

void Launcher::openOnionGameSwitcher(){
#if defined(LAUNCHER_DEVICE)
    const std::string switcherFlag=sdRoot_+"/.tmp_update/.runGameSwitcher";
    const int switcher=open(switcherFlag.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(switcher<0){
        statusMessage_="Could not open OnionOS Game Switcher";
        statusUntil_=SDL_GetTicks64()+2500;
        return;
    }
    if(fsync(switcher)!=0){
        close(switcher);
        unlink(switcherFlag.c_str());
        statusMessage_="Could not save Game Switcher request";
        statusUntil_=SDL_GetTicks64()+2500;
        return;
    }
    close(switcher);
    const int returnFlag=open("/tmp/cartos_return_to_launcher",O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(returnFlag<0){
        unlink(switcherFlag.c_str());
        statusMessage_="Could not queue return to CartOS";
        statusUntil_=SDL_GetTicks64()+2500;
        return;
    }
    fsync(returnFlag);
    close(returnFlag);
    // OnionOS reads the handoff markers as soon as CartOS exits. Flush both
    // directory entries before leaving, especially on the first cold launch.
    sync();
    std::fprintf(stdout,"Queued OnionOS Game Switcher from Menu button\n");
    std::fflush(stdout);
    running_=false;
#else
    statusMessage_="Menu opens OnionOS Game Switcher on device";
    statusUntil_=SDL_GetTicks64()+2000;
#endif
}

void Launcher::beginMenuPress(){
    menuButtonHeld_=true;
    menuButtonChord_=false;
}

void Launcher::endMenuPress(){
    if(!menuButtonHeld_) return;
    const bool openSwitcher=!menuButtonChord_;
    menuButtonHeld_=false;
    menuButtonChord_=false;
    if(openSwitcher) openOnionGameSwitcher();
}

void Launcher::launchGame(const Game& entry){
    std::fprintf(stdout,"Selected: %s [%s]\n",entry.title.c_str(),entry.system.c_str());
    std::fflush(stdout);
    if(entry.launcherPath=="builtin:onion-main-ui"){
#if defined(LAUNCHER_DEVICE)
        shutdown();
        _exit(0);
#else
        running_=false;
        return;
#endif
    }
#if defined(LAUNCHER_DEVICE)
    if(entry.launcherPath.empty()) return;
    const EntryKind kind=entry.kind;
    const std::string launcher=entry.launcherPath;
    const std::string argument=entry.romPath;
    const std::string workingDirectory=entry.workingDirectory;
    const size_t appFolderSlash=workingDirectory.find_last_of('/');
    const std::string appFolder=appFolderSlash==std::string::npos
        ?workingDirectory:workingDirectory.substr(appFolderSlash+1);
    const bool musicPlayer=kind==EntryKind::App &&
        (lower(entry.title)=="music player" || lower(appFolder)=="gmu");
    shutdown();
    if(kind==EntryKind::Game){
        // Replace this app's pending Onion command with a native game command.
        // Onion's runtime then owns the game session and handles GameSwitcher,
        // recents, CPU/audio setup, and normal emulator teardown.
        const std::string commandPath="/mnt/SDCARD/.tmp_update/cmd_to_run.sh";
        const std::string temporaryPath=commandPath+".cartos.tmp";
        const int returnMarker=open("/tmp/cartos_return_to_launcher",O_WRONLY|O_CREAT|O_TRUNC,0644);
        if(returnMarker>=0) close(returnMarker);
        else std::perror("mark CartOS game session");
        if(!writeOnionRecentGame(entry))
            std::fprintf(stderr,"Could not update OnionOS recent games list\n");
        const size_t slash=launcher.find_last_of('/');
        const std::string emuDirectory=slash==std::string::npos?"/mnt/SDCARD/Emu":launcher.substr(0,slash);
        const std::string command="cd "+shellDoubleQuote(emuDirectory)+" && EMU_DIR="+
                                  shellDoubleQuote(emuDirectory)+
                                  " LD_PRELOAD=/mnt/SDCARD/miyoo/lib/libpadsp.so "+
                                  launchInvocation(launcher)+" "+shellDoubleQuote(argument)+"\n";
        {
            std::ofstream output(temporaryPath.c_str(),std::ios::out|std::ios::trunc);
            if(!output){
                std::perror("write OnionOS game command");
                _exit(127);
            }
            output<<command;
            output.close();
            if(!output){
                std::fprintf(stderr,"Could not finish writing OnionOS game command\n");
                _exit(127);
            }
        }
        if(chmod(temporaryPath.c_str(),0755)!=0 || rename(temporaryPath.c_str(),commandPath.c_str())!=0){
            std::perror("install OnionOS game command");
            _exit(127);
        }
        const int quickSwitch=open("/tmp/quick_switch",O_WRONLY|O_CREAT,0644);
        if(quickSwitch<0){
            std::perror("enable OnionOS game handoff");
            _exit(127);
        }
        close(quickSwitch);
        std::fprintf(stdout,"Queued OnionOS game command: %s\n",launcher.c_str());
        std::fflush(stdout);
        _exit(0);
    }
    if(!workingDirectory.empty() && chdir(workingDirectory.c_str())!=0)
        std::perror("change to app directory");
    if(kind==EntryKind::App){
        // Let OnionOS run apps through its normal command loop, just like ROMs.
        // This ensures its runtime handles app teardown and the return hook can
        // reopen CartOS instead of dropping back to MainUI.
        const std::string commandPath="/mnt/SDCARD/.tmp_update/cmd_to_run.sh";
        const std::string temporaryPath=commandPath+".cartos.tmp";
        const std::string markerPath="/tmp/cartos_return_to_launcher";
        const int marker=open(markerPath.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0644);
        if(marker>=0) close(marker);
        else {
            std::perror("mark CartOS app session; launching directly");
            setenv("LD_PRELOAD","/mnt/SDCARD/miyoo/app/../lib/libpadsp.so",1);
            chmod(launcher.c_str(),0755);
            if(launchInvocation(launcher).compare(0,8,"/bin/sh ")==0)
                execl("/bin/sh","sh",launcher.c_str(),static_cast<char*>(nullptr));
            else execl(launcher.c_str(),launcher.c_str(),static_cast<char*>(nullptr));
            std::perror("launch OnionOS entry");
            _exit(127);
        }
        // Mirror the command OnionOS MainUI queues for an app: enter its app
        // directory, make the configured target executable, then launch it
        // with OnionOS's audio/input preload. The runtime also reads the app
        // directory from this command when deciding whether to switch display
        // resolution, so keep the same command shape as MainUI.
        const std::string command=onionAppLaunchCommand(launcher,workingDirectory,sdRoot_,musicPlayer)+"\n";
        {
            std::ofstream output(temporaryPath.c_str(),std::ios::out|std::ios::trunc);
            if(output){
                output<<command;
                output.close();
            }
            if(!output){
                std::fprintf(stderr,"Could not write OnionOS app command; launching directly\n");
                unlink(temporaryPath.c_str());
                unlink(markerPath.c_str());
            }else if(chmod(temporaryPath.c_str(),0755)!=0 || rename(temporaryPath.c_str(),commandPath.c_str())!=0){
                std::perror("install OnionOS app command; launching directly");
                unlink(temporaryPath.c_str());
                unlink(markerPath.c_str());
            }else{
                const int quickSwitch=open("/tmp/quick_switch",O_WRONLY|O_CREAT,0644);
                if(quickSwitch>=0){
                    close(quickSwitch);
                    std::fprintf(stdout,"Queued OnionOS app command: %s\n",launcher.c_str());
                    std::fflush(stdout);
                    _exit(0);
                }
                std::perror("enable OnionOS app handoff; launching directly");
                unlink(markerPath.c_str());
            }
        }
    }
    if(kind==EntryKind::App){
        setenv("LD_PRELOAD","/mnt/SDCARD/miyoo/app/../lib/libpadsp.so",1);
        chmod(launcher.c_str(),0755);
    }
    if(launchInvocation(launcher).compare(0,8,"/bin/sh ")==0)
        execl("/bin/sh","sh",launcher.c_str(),static_cast<char*>(nullptr));
    else execl(launcher.c_str(),launcher.c_str(),static_cast<char*>(nullptr));
    std::perror("launch OnionOS entry");
    _exit(127);
#endif
}

std::string Launcher::collectionPath(const std::string& label) const{
    if(label=="FAVOURITE") return sdRoot_+"/Roms/favourite.json";
    if(label.empty() || label[0]=='-' || label.find('/')!=std::string::npos || label.find('\\')!=std::string::npos ||
       label=="." || label=="..") return {};
    return sdRoot_+"/Emu/_COLLECTIONS/COLLECTIONS/"+label+".json";
}

std::vector<std::string> Launcher::customListNames() const{
    std::vector<std::string> names;
    const std::string directory=sdRoot_+"/Emu/_COLLECTIONS/COLLECTIONS";
    DIR* dir=opendir(directory.c_str());
    if(!dir) return names;
    while(dirent* entry=readdir(dir)){
        const std::string name=entry->d_name;
        if(name.size()<6 || name[0]=='.' || name[0]=='-' || lower(name.substr(name.size()-5))!=".json") continue;
        names.push_back(name.substr(0,name.size()-5));
    }
    closedir(dir);
    std::sort(names.begin(),names.end(),[](const std::string& a,const std::string& b){return lower(a)<lower(b);});
    return names;
}

bool Launcher::writeListEntry(const std::string& path,const Game& game,bool removeExisting){
    const std::string serialized=serializedListEntry(game,sdRoot_);
    if(serialized.empty()) return false;
    const std::string rom=comparableRomPath(game.romPath,sdRoot_);
    const std::string temporary=path+".cartos.tmp";
    std::ofstream output(temporary.c_str(),std::ios::out|std::ios::trunc);
    if(!output) return false;
    std::ifstream input(path.c_str());
    if(access(path.c_str(),F_OK)==0 && !input){output.close();unlink(temporary.c_str());return false;}
    std::string line;
    bool found=false;
    while(std::getline(input,line)){
        const std::string existing=jsonString(line,"rompath");
        if(!existing.empty() && comparableRomPath(existing,sdRoot_)==rom){
            found=true;
            if(removeExisting) continue;
        }
        output<<line<<'\n';
    }
    if(!found || removeExisting) output<<serialized<<'\n';
    output.close();
    if(!output){unlink(temporary.c_str());return false;}
    if(rename(temporary.c_str(),path.c_str())!=0){unlink(temporary.c_str());return false;}
    return true;
}

bool Launcher::removeListEntry(const std::string& path,const Game& game){
    std::ifstream input(path.c_str());
    if(!input) return false;
    const std::string temporary=path+".cartos.tmp";
    std::ofstream output(temporary.c_str(),std::ios::out|std::ios::trunc);
    if(!output) return false;
    const std::string rom=comparableRomPath(game.romPath,sdRoot_);
    std::string line;
    bool removed=false;
    while(std::getline(input,line)){
        const std::string existing=jsonString(line,"rompath");
        if(!existing.empty() && comparableRomPath(existing,sdRoot_)==rom){removed=true;continue;}
        output<<line<<'\n';
    }
    output.close();
    if(!output){unlink(temporary.c_str());return false;}
    if(rename(temporary.c_str(),path.c_str())!=0){unlink(temporary.c_str());return false;}
    return removed;
}

bool Launcher::isSelectedFavorite() const{
    const Category* category=currentCategory();
    if(!category || category->entries.empty()) return false;
    const int selected=wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(category->entries.size()));
    const std::string path=sdRoot_+"/Roms/favourite.json";
    std::ifstream input(path.c_str());
    const std::string target=comparableRomPath(category->entries[static_cast<size_t>(selected)].romPath,sdRoot_);
    std::string line;
    while(std::getline(input,line)){
        const std::string existing=jsonString(line,"rompath");
        if(!existing.empty() && comparableRomPath(existing,sdRoot_)==target) return true;
    }
    return false;
}

void Launcher::openSelectMenu(){
    Category* category=currentCategory();
    if(!category) return;
    menuOptions_.clear();
    if(category->entries.empty()){
        menuOptions_.push_back("Create new list");
        const std::vector<std::string> names=customListNames();
        if(std::find(names.begin(),names.end(),category->label)!=names.end()) menuOptions_.push_back("Delete list...");
        overlaySelection_=0;
        overlay_=Overlay::SelectMenu;
        return;
    }
    if(category->entries[static_cast<size_t>(wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(category->entries.size())))].kind!=EntryKind::Game) return;
    menuOptions_.push_back("Add to list");
    menuOptions_.push_back(isSelectedFavorite()?"Remove from favorites":"Add to favorites");
    menuOptions_.push_back("Create new list");
    const std::vector<std::string> names=customListNames();
    const bool currentIsCustomList=std::find(names.begin(),names.end(),category->label)!=names.end();
    if(category->label=="FAVOURITE" || currentIsCustomList)
        menuOptions_.push_back("Remove from this list");
    if(currentIsCustomList) menuOptions_.push_back("Delete list...");
    menuOptions_.push_back("Game details");
    menuOptions_.push_back("Play a random game");
    menuOptions_.push_back("Delete game...");
    overlaySelection_=0;
    overlay_=Overlay::SelectMenu;
}

void Launcher::addSelectedToList(const std::string& label){
    Category* current=currentCategory();
    if(!current || current->entries.empty()) return;
    const Game game=current->entries[static_cast<size_t>(wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(current->entries.size())))];
    const std::string path=collectionPath(label);
    if(path.empty() || !writeListEntry(path,game,true)){
        statusMessage_="Could not update list";
        statusUntil_=SDL_GetTicks64()+2200;
        return;
    }
    for(Category& category:categories_){
        if(category.label!=label) continue;
        const std::string target=comparableRomPath(game.romPath,sdRoot_);
        const bool exists=std::any_of(category.entries.begin(),category.entries.end(),[&](const Game& item){return comparableRomPath(item.romPath,sdRoot_)==target;});
        if(!exists){Game copy=game;copy.artwork=nullptr;category.entries.push_back(std::move(copy));}
        std::sort(category.entries.begin(),category.entries.end(),[](const Game& a,const Game& b){return lower(a.title)<lower(b.title);});
        break;
    }
    statusMessage_="Added to "+label;
    statusUntil_=SDL_GetTicks64()+2200;
    overlay_=Overlay::None;
}

void Launcher::toggleSelectedFavorite(){
    Category* category=currentCategory();
    if(!category || category->entries.empty()) return;
    const Game game=category->entries[static_cast<size_t>(wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(category->entries.size())))];
    const std::string path=sdRoot_+"/Roms/favourite.json";
    const bool wasFavorite=isSelectedFavorite();
    bool success=false;
    if(wasFavorite){
        success=removeListEntry(path,game);
        if(success) for(Category& item:categories_) if(item.label=="FAVOURITE"){
                for(Game& entry:item.entries) if(comparableRomPath(entry.romPath,sdRoot_)==comparableRomPath(game.romPath,sdRoot_) && entry.artwork){SDL_DestroyTexture(entry.artwork);entry.artwork=nullptr;}
                item.entries.erase(std::remove_if(item.entries.begin(),item.entries.end(),[&](const Game& entry){return comparableRomPath(entry.romPath,sdRoot_)==comparableRomPath(game.romPath,sdRoot_);}),item.entries.end());
            }
    }else{
        success=writeListEntry(path,game,true);
        if(success){
            auto found=std::find_if(categories_.begin(),categories_.end(),[](const Category& item){return item.label=="FAVOURITE";});
            if(found==categories_.end()){
                Category favorites; favorites.label="FAVOURITE"; Game copy=game;copy.artwork=nullptr;favorites.entries.push_back(std::move(copy));
                auto systemApps=std::find_if(categories_.begin(),categories_.end(),[](const Category& item){return item.label=="APPS";});
                const size_t index=static_cast<size_t>(systemApps-categories_.begin());
                categories_.insert(systemApps,std::move(favorites));
                rememberedSelections_.insert(rememberedSelections_.begin()+static_cast<std::ptrdiff_t>(index),0);
                if(index<=categoryIndex_) ++categoryIndex_;
            }else{
                const bool already=std::any_of(found->entries.begin(),found->entries.end(),[&](const Game& entry){return comparableRomPath(entry.romPath,sdRoot_)==comparableRomPath(game.romPath,sdRoot_);});
                if(!already){Game copy=game;copy.artwork=nullptr;found->entries.push_back(std::move(copy));}
                std::sort(found->entries.begin(),found->entries.end(),[](const Game& a,const Game& b){return lower(a.title)<lower(b.title);});
            }
        }
    }
    statusMessage_=success?(wasFavorite?"Removed from favorites":"Added to favorites"):"Could not update favorites";
    statusUntil_=SDL_GetTicks64()+2200;
    overlay_=Overlay::None;
}

void Launcher::removeSelectedFromList(){
    Category* category=currentCategory();
    if(!category || category->entries.empty()) return;
    const Game game=category->entries[static_cast<size_t>(wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(category->entries.size())))];
    const std::string path=collectionPath(category->label);
    if(path.empty() || !removeListEntry(path,game)){
        statusMessage_="Could not remove game from list";
        statusUntil_=SDL_GetTicks64()+2200;
        return;
    }
    for(Game& item:category->entries){
        if(comparableRomPath(item.romPath,sdRoot_)==comparableRomPath(game.romPath,sdRoot_) && item.artwork){SDL_DestroyTexture(item.artwork);item.artwork=nullptr;}
    }
    category->entries.erase(std::remove_if(category->entries.begin(),category->entries.end(),[&](const Game& item){return comparableRomPath(item.romPath,sdRoot_)==comparableRomPath(game.romPath,sdRoot_);}),category->entries.end());
    position_=target_=0.0f;
    rememberedSelections_[categoryIndex_]=0;
    statusMessage_="Removed from "+category->label;
    statusUntil_=SDL_GetTicks64()+2200;
    overlay_=Overlay::None;
}

void Launcher::createList(){
    std::string name=newListName_;
    while(!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());
    while(!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
    if(name.empty() || name[0]=='-' || name=="." || name==".."){
        statusMessage_="Enter a list name first";
        statusUntil_=SDL_GetTicks64()+2200;
        SDL_StartTextInput();
        return;
    }
    for(char& c:name) if(c=='/' || c=='\\' || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|') c='_';
    const std::string path=collectionPath(name);
    if(path.empty() || access(path.c_str(),F_OK)==0){
        statusMessage_=path.empty()?"Invalid list name":"That list already exists";
        statusUntil_=SDL_GetTicks64()+2200;
        SDL_StartTextInput();
        return;
    }
    std::ofstream file(path.c_str(),std::ios::out|std::ios::trunc);
    if(!file){statusMessage_="Could not create list";statusUntil_=SDL_GetTicks64()+2200;SDL_StartTextInput();return;}
    file.close();
    if(!file){statusMessage_="Could not create list";statusUntil_=SDL_GetTicks64()+2200;SDL_StartTextInput();return;}
    Category created; created.label=name;
    auto systemApps=std::find_if(categories_.begin(),categories_.end(),[](const Category& item){return item.label=="APPS";});
    const size_t index=static_cast<size_t>(systemApps-categories_.begin());
    categories_.insert(systemApps,std::move(created));
    rememberedSelections_.insert(rememberedSelections_.begin()+static_cast<std::ptrdiff_t>(index),0);
    if(index<=categoryIndex_) ++categoryIndex_;
    SDL_StopTextInput();
    newListName_.clear();
    overlay_=Overlay::None;
    statusMessage_="Created list: "+name;
    statusUntil_=SDL_GetTicks64()+2200;
}

void Launcher::deleteSelectedList(){
    Category* category=currentCategory();
    if(!category) return;
    const std::vector<std::string> names=customListNames();
    if(std::find(names.begin(),names.end(),category->label)==names.end()){
        statusMessage_="Only named collections can be deleted here";
        statusUntil_=SDL_GetTicks64()+2400;
        overlay_=Overlay::None;
        return;
    }
    const std::string label=category->label;
    const std::string path=collectionPath(label);
    const size_t removedIndex=categoryIndex_;
    clearArtwork(category->entries);
    if(unlink(path.c_str())!=0){
        statusMessage_="Could not delete list";
        statusUntil_=SDL_GetTicks64()+2200;
        overlay_=Overlay::None;
        return;
    }
    categories_.erase(categories_.begin()+static_cast<std::ptrdiff_t>(removedIndex));
    rememberedSelections_.erase(rememberedSelections_.begin()+static_cast<std::ptrdiff_t>(removedIndex));
    if(categories_.empty()) categoryIndex_=0;
    else if(categoryIndex_>=categories_.size()) categoryIndex_=categories_.size()-1;
    position_=target_=static_cast<float>(rememberedSelections_.empty()?0:rememberedSelections_[categoryIndex_]);
    statusMessage_="Deleted list: "+label;
    statusUntil_=SDL_GetTicks64()+2200;
    overlay_=Overlay::None;
}

void Launcher::refreshAll(){
    std::string priorCategory;
    std::string priorRom;
    if(const Category* category=currentCategory()){
        priorCategory=category->label;
        if(!category->entries.empty()){
            const int index=wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(category->entries.size()));
            priorRom=category->entries[static_cast<size_t>(index)].romPath;
        }
    }
    for(Category& category:categories_) clearArtwork(category.entries);
    categories_.clear();
    rememberedSelections_.clear();
    categoryIndex_=0;
    position_=target_=0.0f;
    discoverGames();
    reloadBackgroundChoices();
    for(size_t c=0;c<categories_.size();++c){
        if(categories_[c].label!=priorCategory) continue;
        categoryIndex_=c;
        if(!priorRom.empty()){
            for(size_t i=0;i<categories_[c].entries.size();++i){
                if(comparableRomPath(categories_[c].entries[i].romPath,sdRoot_)==comparableRomPath(priorRom,sdRoot_)){
                    position_=target_=static_cast<float>(i);
                    rememberedSelections_[c]=static_cast<int>(i);
                    break;
                }
            }
        }
        break;
    }
    if(overlay_==Overlay::Search) updateSearchResults();
    statusMessage_="ROMs and lists refreshed";
    statusUntil_=SDL_GetTicks64()+2200;
}

void Launcher::deleteSelectedGame(){
    Category* category=currentCategory();
    if(!category || category->entries.empty()) return;
    const Game game=category->entries[static_cast<size_t>(wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(category->entries.size())))];
    const bool knownConsole=std::any_of(systems_.begin(),systems_.end(),[&](const SystemInfo& system){return game.console==system.id;});
    char resolved[PATH_MAX];
    struct stat romInfo{};
    const std::string expectedPrefix=sdRoot_+"/Roms/"+game.console+"/";
    if(!knownConsole || lstat(game.romPath.c_str(),&romInfo)!=0 || !S_ISREG(romInfo.st_mode) ||
       !realpath(game.romPath.c_str(),resolved) || std::string(resolved).compare(0,expectedPrefix.size(),expectedPrefix)!=0 || unlink(resolved)!=0){
        statusMessage_="Could not safely delete this ROM";
        statusUntil_=SDL_GetTicks64()+2600;
        overlay_=Overlay::None;
        return;
    }
    const std::string target=comparableRomPath(game.romPath,sdRoot_);
    std::vector<std::string> manifests{sdRoot_+"/Roms/favourite.json",sdRoot_+"/Roms/recentlist.json"};
    for(const std::string& name:customListNames()) manifests.push_back(collectionPath(name));
    for(const std::string& manifest:manifests) removeListEntry(manifest,game);
    for(size_t c=0;c<categories_.size();++c){
        std::vector<Game>& entries=categories_[c].entries;
        for(Game& item:entries) if(comparableRomPath(item.romPath,sdRoot_)==target && item.artwork){SDL_DestroyTexture(item.artwork);item.artwork=nullptr;}
        entries.erase(std::remove_if(entries.begin(),entries.end(),[&](const Game& item){return comparableRomPath(item.romPath,sdRoot_)==target;}),entries.end());
        if(c==categoryIndex_ && entries.empty()) position_=target_=0.0f;
    }
    statusMessage_="Deleted game ROM (saves untouched)";
    statusUntil_=SDL_GetTicks64()+2600;
    overlay_=Overlay::None;
}

void Launcher::startSearch(){
    searchQuery_.clear();
    searchResultsFocused_=false;
    searchResultSelection_=0;
    updateSearchResults();
    SDL_StartTextInput();
    overlay_=Overlay::Search;
}

void Launcher::updateSearchResults(){
    searchResults_.clear();
    std::vector<std::string> seen;
    const std::string query=lower(searchQuery_);
    // An empty query is the search screen's recent-games view. Do not treat an
    // empty substring as matching the entire ROM library: that also exposes
    // stale/cached entries and makes the initial view unpredictable.
    size_t firstCategory=0;
    size_t lastCategory=categories_.size();
    if(query.empty()){
        const auto recent=std::find_if(categories_.begin(),categories_.end(),[](const Category& category){return lower(category.label)=="recent";});
        if(recent==categories_.end()) return;
        firstCategory=static_cast<size_t>(std::distance(categories_.begin(),recent));
        lastCategory=firstCategory+1;
    }
    for(size_t c=firstCategory;c<lastCategory;++c){
        for(size_t i=0;i<categories_[c].entries.size();++i){
            const Game& game=categories_[c].entries[i];
            if(game.kind!=EntryKind::Game || game.title.empty() || lower(game.title).find(query)==std::string::npos) continue;
            // Search can outlive edits made from OnionOS or the computer while
            // CartOS stays open. Hide ROMs removed since the library scan.
            if(!regularFile(game.romPath)) continue;
            const std::string key=comparableRomPath(game.romPath,sdRoot_);
            if(std::find(seen.begin(),seen.end(),key)!=seen.end()) continue;
            seen.push_back(key);
            searchResults_.push_back({c,i});
        }
    }
    if(searchResults_.empty()) searchResultSelection_=0;
    else searchResultSelection_=wrapIndex(searchResultSelection_,static_cast<int>(searchResults_.size()));
}

void Launcher::handleSearchInput(const SDL_Event& e){
    if(e.type==SDL_TEXTINPUT && !searchResultsFocused_){
        searchQuery_+=e.text.text;
        updateSearchResults();
        return;
    }
    if(e.type==SDL_KEYDOWN && !searchResultsFocused_){
        const SDL_Keycode key=e.key.keysym.sym;
        if((key>=SDLK_a && key<=SDLK_z) || (key>=SDLK_0 && key<=SDLK_9) || key==SDLK_SPACE || key==SDLK_MINUS){
            searchQuery_+=static_cast<char>(key==SDLK_SPACE?' ':key);
            updateSearchResults();
        }
    }
}

bool Launcher::selectedInSearch(const Game*& game) const{
    if(searchResults_.empty()) return false;
    const auto ref=searchResults_[static_cast<size_t>(wrapIndex(searchResultSelection_,static_cast<int>(searchResults_.size())))];
    if(ref.first>=categories_.size() || ref.second>=categories_[ref.first].entries.size()) return false;
    game=&categories_[ref.first].entries[ref.second];
    return true;
}

long long Launcher::playtimeSecondsFor(const Game& game){
    if(playtimeLookupRom_==game.romPath) return playtimeLookupSeconds_;
    playtimeLookupRom_=game.romPath;
    playtimeLookupSeconds_=-1;

    const std::string romPrefix=sdRoot_+"/Roms/";
    if(game.romPath.compare(0,romPrefix.size(),romPrefix)!=0) return playtimeLookupSeconds_;
    const std::string relativePath=game.romPath.substr(romPrefix.size());
    const std::string databasePath=sdRoot_+"/Saves/CurrentProfile/play_activity/play_activity_db.sqlite";

    // Activity Tracker ships SQLite with OnionOS. Load its C API dynamically
    // so the handheld build and older device toolchains do not need SQLite headers.
    void* library=dlopen((sdRoot_+"/.tmp_update/lib/libsqlite3.so").c_str(),RTLD_NOW|RTLD_LOCAL);
    if(!library) library=dlopen("libsqlite3.so.0",RTLD_NOW|RTLD_LOCAL);
    if(!library) library=dlopen("libsqlite3.so",RTLD_NOW|RTLD_LOCAL);
#ifdef __APPLE__
    if(!library) library=dlopen("/usr/lib/libsqlite3.dylib",RTLD_NOW|RTLD_LOCAL);
#endif
    if(!library) return playtimeLookupSeconds_;

    using Database=void;
    using Statement=void;
    using Open= int(*)(const char*,Database**,int,const char*);
    using Close= int(*)(Database*);
    using Prepare= int(*)(Database*,const char*,int,Statement**,const char**);
    using BindText= int(*)(Statement*,int,const char*,int,void(*)(void*));
    using Step= int(*)(Statement*);
    using ColumnInt64= long long(*)(Statement*,int);
    using Finalize= int(*)(Statement*);
    const auto open=reinterpret_cast<Open>(dlsym(library,"sqlite3_open_v2"));
    const auto close=reinterpret_cast<Close>(dlsym(library,"sqlite3_close"));
    const auto prepare=reinterpret_cast<Prepare>(dlsym(library,"sqlite3_prepare_v2"));
    const auto bindText=reinterpret_cast<BindText>(dlsym(library,"sqlite3_bind_text"));
    const auto step=reinterpret_cast<Step>(dlsym(library,"sqlite3_step"));
    const auto columnInt64=reinterpret_cast<ColumnInt64>(dlsym(library,"sqlite3_column_int64"));
    const auto finalize=reinterpret_cast<Finalize>(dlsym(library,"sqlite3_finalize"));
    if(!open || !close || !prepare || !bindText || !step || !columnInt64 || !finalize){
        dlclose(library);
        return playtimeLookupSeconds_;
    }

    Database* database=nullptr;
    if(open(databasePath.c_str(),&database,1,nullptr)!=0 || !database){
        if(database) close(database);
        dlclose(library);
        return playtimeLookupSeconds_;
    }
    const char* sql="SELECT COALESCE(SUM(pa.play_time),0), COUNT(*) FROM rom r JOIN play_activity pa ON pa.rom_id=r.id WHERE r.file_path=?1";
    Statement* statement=nullptr;
    const bool prepared=prepare(database,sql,-1,&statement,nullptr)==0 && statement;
    if(prepared){
        const auto transient=reinterpret_cast<void(*)(void*)>(static_cast<intptr_t>(-1));
        if(bindText(statement,1,relativePath.c_str(),-1,transient)==0 && step(statement)==100){
            const long long sessions=columnInt64(statement,1);
            if(sessions>0) playtimeLookupSeconds_=columnInt64(statement,0);
        }
        finalize(statement);
    }
    close(database);
    dlclose(library);
    return playtimeLookupSeconds_;
}

void Launcher::launchSearchSelected(){
    if(searchResults_.empty()) return;
    const auto ref=searchResults_[static_cast<size_t>(wrapIndex(searchResultSelection_,static_cast<int>(searchResults_.size())))];
    if(ref.first>=categories_.size() || ref.second>=categories_[ref.first].entries.size()) return;
    beginLaunchAnimation(ref.first,ref.second);
}

void Launcher::activateSelectMenu(){
    if(overlaySelection_<0 || static_cast<size_t>(overlaySelection_)>=menuOptions_.size()) return;
    const std::string action=menuOptions_[static_cast<size_t>(overlaySelection_)];
    if(action=="Add to list"){
        listOptions_=customListNames();
        if(listOptions_.empty()){
            statusMessage_="Create a list first";
            statusUntil_=SDL_GetTicks64()+2200;
            return;
        }
        overlaySelection_=0;
        overlay_=Overlay::AddToList;
    }else if(action=="Add to favorites" || action=="Remove from favorites"){
        toggleSelectedFavorite();
    }else if(action=="Create new list"){
        newListName_.clear();
        virtualKey_=0;
        SDL_StartTextInput();
        overlay_=Overlay::CreateList;
    }else if(action=="Remove from this list"){
        removeSelectedFromList();
    }else if(action=="Game details"){
        playtimeLookupRom_.clear();
        overlay_=Overlay::GameInfo;
    }else if(action=="Play a random game"){
        Category* category=currentCategory();
        if(category && !category->entries.empty()){
            const int index=std::rand()%static_cast<int>(category->entries.size());
            position_=target_=static_cast<float>(index);
            rememberedSelections_[categoryIndex_]=index;
            overlay_=Overlay::None;
            launchSelected();
        }
    }else if(action=="Delete game..."){
        overlaySelection_=1;
        overlay_=Overlay::DeleteConfirm;
    }else if(action=="Delete list..."){
        overlaySelection_=1;
        overlay_=Overlay::DeleteListConfirm;
    }
}

void Launcher::handle(const SDL_Event& e){
    if(e.type==SDL_QUIT){running_=false;return;}
    if(e.type==SDL_KEYUP){
        switch(e.key.keysym.sym){
            case SDLK_LEFT: case SDLK_a: setScrollHeld(0,false); break;
            case SDLK_RIGHT: case SDLK_d: setScrollHeld(1,false); break;
            case SDLK_UP: case SDLK_w: setScrollHeld(2,false); break;
            case SDLK_DOWN: case SDLK_s: setScrollHeld(3,false); break;
            default: break;
        }
    }else if(e.type==SDL_CONTROLLERBUTTONUP){
        switch(e.cbutton.button){
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: setScrollHeld(0,false); break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: setScrollHeld(1,false); break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP: setScrollHeld(2,false); break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: setScrollHeld(3,false); break;
            default: break;
        }
    }else if(e.type==SDL_JOYBUTTONUP && !controller_){
        switch(e.jbutton.button){
            case 2: setScrollHeld(0,false); break;
            case 3: setScrollHeld(1,false); break;
            case 0: setScrollHeld(2,false); break;
            case 1: setScrollHeld(3,false); break;
            default: break;
        }
    }
    if(launchAnimation_){
        const bool confirmPressed=(e.type==SDL_CONTROLLERBUTTONDOWN && e.cbutton.button==SDL_CONTROLLER_BUTTON_A) ||
            (e.type==SDL_KEYDOWN && !e.key.repeat && (e.key.keysym.sym==SDLK_RETURN || e.key.keysym.sym==SDLK_SPACE));
        const bool previewLeft=(e.type==SDL_KEYDOWN && !e.key.repeat &&
                                (e.key.keysym.sym==SDLK_LEFT || e.key.keysym.sym==SDLK_a)) ||
                               (e.type==SDL_CONTROLLERBUTTONDOWN && e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_LEFT) ||
                               (e.type==SDL_JOYBUTTONDOWN && !controller_ && e.jbutton.button==2) ||
                               (e.type==SDL_JOYHATMOTION && (e.jhat.value&SDL_HAT_LEFT)!=0);
        const bool previewRight=(e.type==SDL_KEYDOWN && !e.key.repeat &&
                                 (e.key.keysym.sym==SDLK_RIGHT || e.key.keysym.sym==SDLK_d)) ||
                                (e.type==SDL_CONTROLLERBUTTONDOWN && e.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
                                (e.type==SDL_JOYBUTTONDOWN && !controller_ && e.jbutton.button==3) ||
                                (e.type==SDL_JOYHATMOTION && (e.jhat.value&SDL_HAT_RIGHT)!=0);
        if(launchAwaitingConfirm_){
            if(confirmPressed){
                resetPreviewArtwork();
                launchAwaitingConfirm_=false;
                launchStepTwoConfirmed_=true;
            }else if(previewLeft){
                browsePreviewPage(-1);
            }else if(previewRight){
                browsePreviewPage(1);
            }else if((e.type==SDL_CONTROLLERBUTTONDOWN && e.cbutton.button==SDL_CONTROLLER_BUTTON_B) ||
                     (e.type==SDL_KEYDOWN && !e.key.repeat && (e.key.keysym.sym==SDLK_ESCAPE || e.key.keysym.sym==SDLK_BACKSPACE))){
                // Restore the primary artwork before the first reverse frame,
                // so B never briefly shows an alternate image in the browser.
                resetPreviewArtwork();
                launchAwaitingConfirm_=false;
                launchReversing_=true;
                launchReverseElapsed_=0.0f;
                launchStepTwoConfirmed_=false;
                if(launchCenterDuration_>0.0f){
                    // The reverse should end at the centered browser position.
                    // Keep the selected cartridge anchored while the entrance
                    // plays backward, avoiding a return to the pre-launch
                    // fractional offset followed by a second settling motion.
                    const float distance=std::fabs(position_-launchCenteringPosition_);
                    const float offCenterBase=(280.0f-95.0f*std::min(1.0f,distance))/280.0f;
                    launchReverseStartScale_=1.34f*offCenterBase;
                    position_=launchCenteringPosition_;
                    target_=launchCenteringPosition_;
                    launchCenteringOffset_=0.0f;
                }else{
                    // Search's ordinary cartridge is 220 px wide; its launch
                    // view begins at the 280 px browser size before enlarging.
                    launchReverseStartScale_=(280.0f/220.0f)*1.34f;
                }
            }
        }else if(confirmPressed && !launchReversing_ &&
                 SDL_GetTicks64()-launchFirstPressAt_<=kLaunchDoubleTapWindowMs){
            launchStepTwoConfirmed_=true;
        }
        return;
    }

    const auto isMenuButton=[](Uint8 button){
        return button==SDL_CONTROLLER_BUTTON_GUIDE || button==SDL_CONTROLLER_BUTTON_MISC1;
    };
    if(e.type==SDL_CONTROLLERBUTTONDOWN && isMenuButton(e.cbutton.button)){
        if(!menuButtonHeld_) beginMenuPress();
        return;
    }
    if(e.type==SDL_CONTROLLERBUTTONUP && isMenuButton(e.cbutton.button)){
        endMenuPress();
        return;
    }
    if(e.type==SDL_KEYDOWN && !e.key.repeat && (e.key.keysym.sym==SDLK_MENU || e.key.keysym.sym==SDLK_HOME)){
        if(!menuButtonHeld_) beginMenuPress();
        return;
    }
    if(e.type==SDL_KEYUP && (e.key.keysym.sym==SDLK_MENU || e.key.keysym.sym==SDLK_HOME)){
        endMenuPress();
        return;
    }
    if(menuButtonHeld_ && ((e.type==SDL_CONTROLLERBUTTONDOWN && !isMenuButton(e.cbutton.button)) ||
                           e.type==SDL_CONTROLLERAXISMOTION ||
                           (e.type==SDL_KEYDOWN && !e.key.repeat)))
        menuButtonChord_=true;

    if(overlay_==Overlay::Search){
        if(e.type==SDL_TEXTINPUT){if(!searchResultsFocused_)handleSearchInput(e);return;}
        if(e.type==SDL_KEYDOWN && !e.key.repeat){
            const SDL_Keycode key=e.key.keysym.sym;
            if(!searchResultsFocused_){
                if(key==SDLK_BACKSPACE){if(!searchQuery_.empty()) searchQuery_.pop_back();updateSearchResults();}
                else if(key==SDLK_RETURN || key==SDLK_TAB) searchResultsFocused_=true;
                else if(key==SDLK_ESCAPE){SDL_StopTextInput();overlay_=Overlay::None;}
                else if(key==SDLK_UP || key==SDLK_w) virtualKey_=std::max(0,virtualKey_-10);
                else if(key==SDLK_DOWN || key==SDLK_s) virtualKey_=std::min(39,virtualKey_+10);
                else if(key==SDLK_LEFT || key==SDLK_a) virtualKey_=std::max(0,virtualKey_-1);
                else if(key==SDLK_RIGHT || key==SDLK_d) virtualKey_=std::min(39,virtualKey_+1);
                else if(key==SDLK_x)refreshAll();
                else if(key==SDLK_y) searchResultsFocused_=true;
            }else{
                if(key==SDLK_LEFT || key==SDLK_a) --searchResultSelection_;
                else if(key==SDLK_RIGHT || key==SDLK_d) ++searchResultSelection_;
                else if(key==SDLK_UP || key==SDLK_w) --searchResultSelection_;
                else if(key==SDLK_DOWN || key==SDLK_s) ++searchResultSelection_;
                else if(key==SDLK_RETURN) launchSearchSelected();
                else if(key==SDLK_ESCAPE || key==SDLK_BACKSPACE || key==SDLK_TAB) searchResultsFocused_=false;
                else if(key==SDLK_y) searchResultsFocused_=false;
            }
            return;
        }
        if(e.type==SDL_CONTROLLERBUTTONDOWN){
            const Uint8 button=e.cbutton.button;
            if(button==SDL_CONTROLLER_BUTTON_B){
                if(searchResultsFocused_) searchResultsFocused_=false;
                else if(!searchQuery_.empty()){searchQuery_.pop_back();updateSearchResults();}
                else {SDL_StopTextInput();overlay_=Overlay::None;}
            }else if(button==SDL_CONTROLLER_BUTTON_X)refreshAll();
            else if(button==SDL_CONTROLLER_BUTTON_Y) searchResultsFocused_=!searchResultsFocused_;
            else if(button==SDL_CONTROLLER_BUTTON_START) searchResultsFocused_=true;
            else if(button==SDL_CONTROLLER_BUTTON_A){
                if(searchResultsFocused_) launchSearchSelected();
                else{
                    searchQuery_.push_back(searchKeyboardCharacter(virtualKey_));
                    updateSearchResults();
                }
            }else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT){if(searchResultsFocused_)--searchResultSelection_;else virtualKey_=std::max(0,virtualKey_-1);}
            else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT){if(searchResultsFocused_)++searchResultSelection_;else virtualKey_=std::min(39,virtualKey_+1);}
            else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP){if(searchResultsFocused_)--searchResultSelection_;else virtualKey_=std::max(0,virtualKey_-10);}
            else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN){if(searchResultsFocused_)++searchResultSelection_;else virtualKey_=std::min(39,virtualKey_+10);}
            else if(button==SDL_CONTROLLER_BUTTON_BACK){SDL_StopTextInput();overlay_=Overlay::None;}
            return;
        }
        if(e.type==SDL_JOYBUTTONDOWN && !controller_){
            const int button=e.jbutton.button;
            if(button==5){if(searchResultsFocused_)searchResultsFocused_=false;else if(!searchQuery_.empty()){searchQuery_.pop_back();updateSearchResults();}else{SDL_StopTextInput();overlay_=Overlay::None;}}
            else if(button==6)refreshAll();
            else if(button==7)searchResultsFocused_=!searchResultsFocused_;
            else if(button==4){if(searchResultsFocused_)launchSearchSelected();else{searchQuery_.push_back(searchKeyboardCharacter(virtualKey_));updateSearchResults();}}
            else if(button==10)searchResultsFocused_=true;
            else if(button==8){SDL_StopTextInput();overlay_=Overlay::None;}
            return;
        }
        if(e.type==SDL_JOYHATMOTION && !controller_){
            if(searchResultsFocused_){if(e.jhat.value&SDL_HAT_LEFT)--searchResultSelection_;else if(e.jhat.value&SDL_HAT_RIGHT)++searchResultSelection_;}
            else if(e.jhat.value&SDL_HAT_LEFT)virtualKey_=std::max(0,virtualKey_-1);else if(e.jhat.value&SDL_HAT_RIGHT)virtualKey_=std::min(39,virtualKey_+1);else if(e.jhat.value&SDL_HAT_UP)virtualKey_=std::max(0,virtualKey_-10);else if(e.jhat.value&SDL_HAT_DOWN)virtualKey_=std::min(39,virtualKey_+10);
            return;
        }
        return;
    }

    if(overlay_!=Overlay::None){
        if(overlay_==Overlay::Settings){
            if(e.type==SDL_KEYDOWN && !e.key.repeat){
                const SDL_Keycode key=e.key.keysym.sym;
                if(key==SDLK_ESCAPE || key==SDLK_BACKSPACE){overlay_=Overlay::None;return;}
                if(key==SDLK_UP || key==SDLK_w) setScrollHeld(2,true);
                else if(key==SDLK_DOWN || key==SDLK_s) setScrollHeld(3,true);
                else if(key==SDLK_LEFT || key==SDLK_a) changeSetting(-1);
                else if(key==SDLK_RIGHT || key==SDLK_d) changeSetting(1);
                else if(key==SDLK_x) refreshAll();
                else if(key==SDLK_RETURN || key==SDLK_SPACE) changeSetting(1);
            }else if(e.type==SDL_CONTROLLERBUTTONDOWN){
                const Uint8 button=e.cbutton.button;
                if(button==SDL_CONTROLLER_BUTTON_B){overlay_=Overlay::None;return;}
                if(button==SDL_CONTROLLER_BUTTON_DPAD_UP) setScrollHeld(2,true);
                else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN) setScrollHeld(3,true);
                else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT) changeSetting(-1);
                else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT || button==SDL_CONTROLLER_BUTTON_A) changeSetting(1);
                else if(button==SDL_CONTROLLER_BUTTON_X) refreshAll();
            }
            return;
        }
        if(e.type==SDL_KEYDOWN && !e.key.repeat){
            const SDL_Keycode key=e.key.keysym.sym;
            if(overlay_==Overlay::CreateList){
                if(key==SDLK_ESCAPE){SDL_StopTextInput();overlay_=Overlay::None;}
                else if(key==SDLK_BACKSPACE){if(!newListName_.empty())newListName_.pop_back();}
                else if(key==SDLK_RETURN)createList();
                else if(key==SDLK_UP || key==SDLK_w)virtualKey_=std::max(0,virtualKey_-10);
                else if(key==SDLK_DOWN || key==SDLK_s)virtualKey_=std::min(39,virtualKey_+10);
                else if(key==SDLK_LEFT || key==SDLK_a)virtualKey_=std::max(0,virtualKey_-1);
                else if(key==SDLK_RIGHT || key==SDLK_d)virtualKey_=std::min(39,virtualKey_+1);
                return;
            }
            if(key==SDLK_ESCAPE || key==SDLK_BACKSPACE){overlay_=Overlay::None;return;}
            if(key==SDLK_UP || key==SDLK_w)--overlaySelection_;
            else if(key==SDLK_DOWN || key==SDLK_s)++overlaySelection_;
            else if(key==SDLK_RETURN || key==SDLK_SPACE){
                if(overlay_==Overlay::SelectMenu) activateSelectMenu();
                else if(overlay_==Overlay::AddToList && !listOptions_.empty()) addSelectedToList(listOptions_[static_cast<size_t>(wrapIndex(overlaySelection_,static_cast<int>(listOptions_.size())))]);
                else if(overlay_==Overlay::DeleteConfirm){const bool confirm=overlaySelection_==0;overlay_=Overlay::None;if(confirm)deleteSelectedGame();}
                else if(overlay_==Overlay::DeleteListConfirm){const bool confirm=overlaySelection_==0;overlay_=Overlay::None;if(confirm)deleteSelectedList();}
            }
        }else if(e.type==SDL_TEXTINPUT && overlay_==Overlay::CreateList){newListName_+=e.text.text;}
        else if(e.type==SDL_CONTROLLERBUTTONDOWN){
            const Uint8 button=e.cbutton.button;
            if(overlay_==Overlay::CreateList){
                static const std::string keys[]={"ABCDEFGHIJ","KLMNOPQRST","UVWXYZ0123","456789 -  "};
                if(button==SDL_CONTROLLER_BUTTON_B){SDL_StopTextInput();overlay_=Overlay::None;}
                else if(button==SDL_CONTROLLER_BUTTON_X){if(!newListName_.empty())newListName_.pop_back();}
                else if(button==SDL_CONTROLLER_BUTTON_START)createList();
                else if(button==SDL_CONTROLLER_BUTTON_A){const char value=keys[std::min(3,virtualKey_/10)][static_cast<size_t>(virtualKey_%10)];if(value!=' ')newListName_.push_back(value);}
                else if(button==SDL_CONTROLLER_BUTTON_DPAD_LEFT)virtualKey_=std::max(0,virtualKey_-1);
                else if(button==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)virtualKey_=std::min(39,virtualKey_+1);
                else if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)virtualKey_=std::max(0,virtualKey_-10);
                else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)virtualKey_=std::min(39,virtualKey_+10);
                return;
            }
            if(button==SDL_CONTROLLER_BUTTON_B){overlay_=Overlay::None;return;}
            if(button==SDL_CONTROLLER_BUTTON_DPAD_UP)--overlaySelection_;
            else if(button==SDL_CONTROLLER_BUTTON_DPAD_DOWN)++overlaySelection_;
            else if(button==SDL_CONTROLLER_BUTTON_A || button==SDL_CONTROLLER_BUTTON_START){
                if(overlay_==Overlay::SelectMenu)activateSelectMenu();
                else if(overlay_==Overlay::AddToList && !listOptions_.empty())addSelectedToList(listOptions_[static_cast<size_t>(wrapIndex(overlaySelection_,static_cast<int>(listOptions_.size())))]);
                else if(overlay_==Overlay::DeleteConfirm){const bool confirm=overlaySelection_==0;overlay_=Overlay::None;if(confirm)deleteSelectedGame();}
                else if(overlay_==Overlay::DeleteListConfirm){const bool confirm=overlaySelection_==0;overlay_=Overlay::None;if(confirm)deleteSelectedList();}
            }
        }
        if(overlay_==Overlay::SelectMenu && !menuOptions_.empty()) overlaySelection_=wrapIndex(overlaySelection_,static_cast<int>(menuOptions_.size()));
        else if(overlay_==Overlay::AddToList && !listOptions_.empty()) overlaySelection_=wrapIndex(overlaySelection_,static_cast<int>(listOptions_.size()));
        else if(overlay_==Overlay::DeleteConfirm) overlaySelection_=wrapIndex(overlaySelection_,2);
        else if(overlay_==Overlay::DeleteListConfirm) overlaySelection_=wrapIndex(overlaySelection_,2);
        return;
    }

    if(e.type==SDL_KEYDOWN&&!e.key.repeat){
        switch(e.key.keysym.sym){
            case SDLK_LEFT: case SDLK_a: setScrollHeld(0,true); break;
            case SDLK_RIGHT: case SDLK_d: setScrollHeld(1,true); break;
            case SDLK_UP: case SDLK_w: setScrollHeld(2,true); break;
            case SDLK_DOWN: case SDLK_s: setScrollHeld(3,true); break;
            case SDLK_RETURN: case SDLK_SPACE: case SDLK_LALT: launchSelected(); break;
            case SDLK_RSHIFT: case SDLK_TAB: openSelectMenu(); break;
            case SDLK_y: startSearch(); break;
            case SDLK_x: refreshAll(); break;
            case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_LCTRL: running_=false; break;
        }
    }
    if(e.type==SDL_CONTROLLERBUTTONDOWN){
        switch(e.cbutton.button){
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: setScrollHeld(0,true); break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: setScrollHeld(1,true); break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP: setScrollHeld(2,true); break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: setScrollHeld(3,true); break;
            case SDL_CONTROLLER_BUTTON_A: launchSelected(); break;
            case SDL_CONTROLLER_BUTTON_B: break;
            case SDL_CONTROLLER_BUTTON_BACK: openSelectMenu(); break;
            case SDL_CONTROLLER_BUTTON_Y: startSearch(); break;
            case SDL_CONTROLLER_BUTTON_X: refreshAll(); break;
            default: break;
        }
    }
    if(e.type==SDL_JOYHATMOTION && !controller_){
        setScrollHeld(0,(e.jhat.value&SDL_HAT_LEFT)!=0);
        setScrollHeld(1,(e.jhat.value&SDL_HAT_RIGHT)!=0);
        setScrollHeld(2,(e.jhat.value&SDL_HAT_UP)!=0);
        setScrollHeld(3,(e.jhat.value&SDL_HAT_DOWN)!=0);
    }
    if(e.type==SDL_JOYBUTTONDOWN && !controller_){
#if defined(LAUNCHER_DEVICE)
        switch(e.jbutton.button){
            case 0: setScrollHeld(2,true); break;
            case 1: setScrollHeld(3,true); break;
            case 2: setScrollHeld(0,true); break;
            case 3: setScrollHeld(1,true); break;
            case 4: launchSelected(); break;
            case 5: break;
            case 7: startSearch(); break;
            case 6: refreshAll(); break;
            case 10: openSelectMenu(); break;
            default: break;
        }
#else
        if(e.jbutton.button==0) launchSelected();
#endif
    }
}

namespace {
void arrow(SDL_Renderer* renderer,int x,int y,bool pointsUp,SDL_Color color){
    SDL_SetRenderDrawColor(renderer,color.r,color.g,color.b,color.a);
    if(pointsUp){
        SDL_RenderDrawLine(renderer,x,y+8,x,y+2);
        SDL_RenderDrawLine(renderer,x,y+2,x-4,y+6);
        SDL_RenderDrawLine(renderer,x,y+2,x+4,y+6);
    }else{
        SDL_RenderDrawLine(renderer,x,y,x,y+6);
        SDL_RenderDrawLine(renderer,x,y+6,x-4,y+2);
        SDL_RenderDrawLine(renderer,x,y+6,x+4,y+2);
    }
}

void fillRect(SDL_Renderer* renderer,const SDL_Rect& rect,SDL_Color color){
    SDL_SetRenderDrawColor(renderer,color.r,color.g,color.b,color.a);
    SDL_RenderFillRect(renderer,&rect);
}

void outlineRect(SDL_Renderer* renderer,const SDL_Rect& rect,SDL_Color color){
    SDL_SetRenderDrawColor(renderer,color.r,color.g,color.b,color.a);
    SDL_RenderDrawRect(renderer,&rect);
}

std::string shortLabel(std::string value,size_t limit=30){
    if(value.size()>limit) return value.substr(0,limit-3)+"...";
    return value;
}
}

void Launcher::render(float dt){
    const Uint32 now=SDL_GetTicks();
    if(batteryPollAt_==0 || static_cast<Uint32>(now-batteryPollAt_)>=5000){
        batteryPercent_=platform::batteryPercent();
        batteryPollAt_=now;
    }
    if(overlay_==Overlay::Search){renderSearch();return;}
    Category* category=currentCategory();
    if(!category || category->entries.empty()){
        renderBackground();
        if(category){
            draw::text(renderer_,18,38,category->label,26,white);
            const std::string empty="EMPTY LIST";
            draw::text(renderer_,(kDesignW-draw::textWidth(empty,30))/2,kDesignH/2,empty,30,white);
        }else{
            const std::string empty="NO LISTS FOUND";
            draw::text(renderer_,(kDesignW-draw::textWidth(empty,30))/2,kDesignH/2,empty,30,white);
        }
        if(overlay_==Overlay::SelectMenu){
            SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
            fillRect(renderer_,SDL_Rect{82,154,588,246},{4,7,15,240});
            outlineRect(renderer_,SDL_Rect{82,154,588,246},{70,82,100,255});
            draw::text(renderer_,(kDesignW-draw::textWidth("OPTIONS",26))/2,182,"OPTIONS",26,white);
            for(size_t i=0;i<menuOptions_.size();++i){
                const std::string item=(static_cast<int>(i)==overlaySelection_?"> ":"  ")+menuOptions_[i];
                draw::text(renderer_,(kDesignW-draw::textWidth(item,22))/2,238+static_cast<int>(i)*38,item,22,static_cast<int>(i)==overlaySelection_?SDL_Color{145,205,255,255}:white);
            }
            const std::string footer="A SELECT   B BACK";
            draw::text(renderer_,(kDesignW-draw::textWidth(footer,16))/2,347,footer,16,muted);
            SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
        }else if(overlay_==Overlay::CreateList){
            SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
            fillRect(renderer_,SDL_Rect{58,85,636,390},{4,7,15,238});
            outlineRect(renderer_,SDL_Rect{58,85,636,390},{70,82,100,255});
            draw::text(renderer_,(kDesignW-draw::textWidth("CREATE NEW LIST",27))/2,105,"CREATE NEW LIST",27,white);
            const std::string name=newListName_.empty()?"TYPE A NAME":"NAME: "+newListName_;
            draw::text(renderer_,(kDesignW-draw::textWidth(name,23))/2,153,name,23,{145,205,255,255});
            static const std::string keys[]={"ABCDEFGHIJ","KLMNOPQRST","UVWXYZ0123","456789 -  "};
            for(int row=0;row<4;++row) for(int col=0;col<10;++col){
                const int index=row*10+col;
                const SDL_Rect keyRect{116+col*53,204+row*42,45,32};
                if(index==virtualKey_)fillRect(renderer_,keyRect,{41,70,98,255});
                outlineRect(renderer_,keyRect,index==virtualKey_?SDL_Color{145,205,255,255}:SDL_Color{55,63,76,255});
                const char letter=keys[row][static_cast<size_t>(col)];
                const std::string caption=letter==' '?"·":std::string(1,letter);
                draw::text(renderer_,keyRect.x+(keyRect.w-draw::textWidth(caption,17))/2,keyRect.y+7,caption,17,index==virtualKey_?SDL_Color{145,205,255,255}:white);
            }
            const std::string footer="A ADD   X BACKSPACE   START CREATE   B CANCEL";
            draw::text(renderer_,(kDesignW-draw::textWidth(footer,15))/2,425,footer,15,muted);
            SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
        }else if(overlay_==Overlay::DeleteListConfirm){
            SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
            fillRect(renderer_,SDL_Rect{82,154,588,246},{4,7,15,240});
            outlineRect(renderer_,SDL_Rect{82,154,588,246},{70,82,100,255});
            draw::text(renderer_,(kDesignW-draw::textWidth("DELETE LIST?",28))/2,183,"DELETE LIST?",28,white);
            const std::string label=currentCategory()?currentCategory()->label:"";
            draw::text(renderer_,(kDesignW-draw::textWidth(shortLabel(label,36),20))/2,234,shortLabel(label,36),20,{145,205,255,255});
            draw::text(renderer_,(kDesignW-draw::textWidth("Games stay on the SD card.",17))/2,268,"Games stay on the SD card.",17,muted);
            const std::array<std::string,2> choices{{"DELETE LIST","CANCEL"}};
            for(int i=0;i<2;++i){const SDL_Rect row{202,300+i*44,348,36};if(i==overlaySelection_)fillRect(renderer_,row,{32,50,72,255});draw::text(renderer_,(kDesignW-draw::textWidth(choices[static_cast<size_t>(i)],20))/2,row.y+5,choices[static_cast<size_t>(i)],20,i==overlaySelection_?SDL_Color{145,205,255,255}:white);}
            const std::string footer="UP/DOWN CHOOSE   A CONFIRM   B CANCEL";
            draw::text(renderer_,(kDesignW-draw::textWidth(footer,15))/2,374,footer,15,muted);
            SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
        }
        SDL_RenderPresent(renderer_);
        return;
    }

    std::vector<Game>& entries=category->entries;
    // Settle the carousel quickly enough that held-key repeats do not stack
    // multiple moving cartridge positions on the Miyoo LCD.
    if(!launchAnimation_) position_+=(target_-position_)*(1.0f-std::exp(-dt*28.0f));
    const int entryCount=static_cast<int>(entries.size());
    const float count=static_cast<float>(entryCount);
    if(!wrapGameBrowsing_){
        position_=std::max(0.0f,std::min(position_,count-1.0f));
        target_=std::max(0.0f,std::min(target_,count-1.0f));
    }
    if(std::fabs(target_-position_)<0.001f){
        position_=target_;
        if(wrapGameBrowsing_){
            const float turns=std::floor(position_/count);
            position_-=turns*count; target_-=turns*count;
        }
    }
    rememberedSelections_[categoryIndex_]=wrapGameBrowsing_
        ?wrapIndex(static_cast<int>(std::lround(target_)),entryCount)
        :std::max(0,std::min(static_cast<int>(std::lround(target_)),entryCount-1));
    renderBackground();

    // Keep the carousel's geometric center unwrapped while it animates across
    // either end. Only the entry lookup wraps; using the wrapped index for the
    // x-coordinate makes a one-step move appear to jump by the full list size.
    const int centerUnwrapped=static_cast<int>(std::lround(position_));
    const std::array<int,3> drawOrder{{-1,1,0}};
    std::array<int,3> visible{{-1,-1,-1}};
    for(int offset=-1;offset<=1;++offset){
        const int candidate=centerUnwrapped+offset;
        if(!wrapGameBrowsing_ && (candidate<0 || candidate>=entryCount)) continue;
        visible[static_cast<size_t>(offset+1)]=wrapGameBrowsing_?wrapIndex(candidate,entryCount):candidate;
    }
    for(int offset:drawOrder){
        const int candidate=centerUnwrapped+offset;
        if(!wrapGameBrowsing_ && (candidate<0 || candidate>=entryCount)) continue;
        const int idx=wrapGameBrowsing_?wrapIndex(candidate,entryCount):candidate;
        Game& entry=entries[static_cast<size_t>(idx)];
        if(!entry.artwork && !entry.artworkAttempted && !entry.artPath.empty()){
            entry.artworkAttempted=true;
            entry.artwork=artwork::loadPng(renderer_,entry.artPath);
        }
        const float delta=static_cast<float>(candidate)-position_;
        const float distance=std::fabs(delta);
        float scale=1.0f;
        const float spacing=carouselSpacing_==0?215.0f:(carouselSpacing_==2?255.0f:240.0f);
        int cx=kDesignW/2+static_cast<int>(delta*spacing);
        int cy=254;
        float liftEase=0.0f;
        float dropEase=0.0f;
        float startupSideEase=1.0f;
        float startupCenterEase=1.0f;
        if(startupAnimation_){
            auto smooth=[](float value){
                const float t=std::max(0.0f,std::min(1.0f,value));
                return t*t*(3.0f-2.0f*t);
            };
            startupCenterEase=smooth(startupAnimationElapsed_/0.48f);
            startupSideEase=smooth((startupAnimationElapsed_-0.48f)/0.40f);
            if(offset!=0){
                const float direction=offset<0?-1.0f:1.0f;
                cx+=static_cast<int>(direction*460.0f*(1.0f-startupSideEase));
            }
        }
        if(launchAnimation_){
            const float animationTime=std::max(0.0f,launchAnimationElapsed_-launchCenterDuration_);
            const float reverseProgress=std::min(1.0f,launchReverseElapsed_/kLaunchReverseDuration);
            const float reverseEase=reverseProgress*reverseProgress*(3.0f-2.0f*reverseProgress);
            const float centerProgress=launchCenterDuration_>0.0f
                ?std::min(1.0f,launchAnimationElapsed_/launchCenterDuration_):1.0f;
            const float centerEase=centerProgress*centerProgress*(3.0f-2.0f*centerProgress);
            cx+=static_cast<int>(launchCenteringOffset_*centerEase);
            const float grow=std::min(1.0f,animationTime/0.38f);
            const float growEase=1.0f-std::pow(1.0f-grow,3.0f);
            if(offset==0){
                // B starts from the full-size stage and only shrinks; it does
                // not rewind the entrance growth curve.
                scale=launchReversing_?launchReverseStartScale_+(1.0f-launchReverseStartScale_)*reverseEase:1.0f+0.34f*growEase;
                // Let the enlarged cartridge register, lift it a touch, then
                // ease it down offscreen as one continuous movement.
                const float lift=std::max(0.0f,std::min(1.0f,(animationTime-0.50f)/0.14f));
                liftEase=lift*lift*(3.0f-2.0f*lift);
                const float drop=std::max(0.0f,std::min(1.0f,(animationTime-0.64f)/0.36f));
                dropEase=drop*drop*(3.0f-2.0f*drop);
            }else{
                const float slide=std::max(0.0f,std::min(1.0f,animationTime/0.48f));
                const float slideEase=launchReversing_?1.0f-reverseEase:1.0f-std::pow(1.0f-slide,3.0f);
                cx+=static_cast<int>((offset<0?-1.0f:1.0f)*slideEase*500.0f);
            }
        }
        const int selectedSystemScale=cartridgeScaleFor(entry.console);
        const float scalePreference=selectedSystemScale==0?0.90f:(selectedSystemScale==2?1.10f:1.0f);
        const int cw=static_cast<int>((280-95*std::min(1.0f,distance))*scale*scalePreference);
        const int ch=static_cast<int>((350-120*std::min(1.0f,distance))*scale*scalePreference);
        if(launchAnimation_ && offset==0){
            const int liftedCenterY=254-static_cast<int>(30.0f*liftEase);
            const int fullyOffscreenCenterY=kDesignH+ch/2+30;
            cy=liftedCenterY+static_cast<int>((fullyOffscreenCenterY-liftedCenterY)*dropEase);
        }else if(startupAnimation_ && offset==0){
            const int startY=kDesignH+ch/2+8;
            cy=startY+static_cast<int>((254-startY)*startupCenterEase);
        }
        const SDL_Rect box{cx-cw/2,cy-ch/2,cw,ch};
        if(!(launchAwaitingConfirm_ && previewShowingDetails_ && offset==0)){
            if(entry.kind==EntryKind::App) draw::appIcon(renderer_,box,entry.artwork,entry.variant,entry.onionMainUi);
            else draw::cartridge(renderer_,box,entry.system,entry.title,entry.variant,distance<0.5f,entry.artwork);
        }
    }
    for(size_t i=0;i<entries.size();++i){
        if(entries[i].artwork && std::find(visible.begin(),visible.end(),static_cast<int>(i))==visible.end()){
            SDL_DestroyTexture(entries[i].artwork);
            entries[i].artwork=nullptr;
            entries[i].artworkAttempted=false;
        }
    }

    const int chosen=wrapGameBrowsing_
        ?wrapIndex(static_cast<int>(std::lround(position_)),entryCount)
        :std::max(0,std::min(static_cast<int>(std::lround(position_)),entryCount-1));
    std::string title=entries[static_cast<size_t>(chosen)].title;
    const int titleHeight=titleSize_==0?26:(titleSize_==2?34:30);
    while(!title.empty() && draw::textWidth(title,titleHeight)>kDesignW-40){
        size_t cut=title.size()-1;
        while(cut>0 && (static_cast<unsigned char>(title[cut])&0xC0)==0x80) --cut;
        title.erase(cut);
    }
    int bottomTextOffset=0;
    int topTextOffset=0;
    if(launchAnimation_){
        const float animationTime=std::max(0.0f,launchAnimationElapsed_-launchCenterDuration_);
        const float topProgress=std::min(1.0f,animationTime/0.40f);
        const float bottomProgress=std::min(1.0f,animationTime/0.40f);
        const float reverseProgress=std::min(1.0f,launchReverseElapsed_/kLaunchReverseDuration);
        const float reverseEase=reverseProgress*reverseProgress*(3.0f-2.0f*reverseProgress);
        const float topEase=launchReversing_?1.0f-reverseEase:topProgress*topProgress*(3.0f-2.0f*topProgress);
        const float bottomEase=launchReversing_?1.0f-reverseEase:bottomProgress*bottomProgress*(3.0f-2.0f*bottomProgress);
        topTextOffset=-190*static_cast<int>(topEase*1000.0f)/1000;
        bottomTextOffset=190*static_cast<int>(bottomEase*1000.0f)/1000;
    }else if(startupAnimation_){
        const float textProgress=std::max(0.0f,std::min(1.0f,(startupAnimationElapsed_-0.82f)/0.36f));
        const float textEase=textProgress*textProgress*(3.0f-2.0f*textProgress);
        topTextOffset=-190*static_cast<int>((1.0f-textEase)*1000.0f)/1000;
        bottomTextOffset=190*static_cast<int>((1.0f-textEase)*1000.0f)/1000;
    }
    const bool invertBrowserText=invertTextColour_ && overlay_==Overlay::None;
    const auto browserTextColor=[invertBrowserText](SDL_Color color){
        if(!invertBrowserText) return color;
        const unsigned luminance=(static_cast<unsigned>(color.r)*30u+
                                  static_cast<unsigned>(color.g)*59u+
                                  static_cast<unsigned>(color.b)*11u)/100u;
        if(luminance>=200u){
            color.r=18; color.g=22; color.b=30;
        }else if(luminance>=140u){
            color.r=42; color.g=48; color.b=58;
        }else{
            color.r=60; color.g=66; color.b=76;
        }
        return color;
    };
    if(!hideGameTitle_)
        draw::text(renderer_,(kDesignW-draw::textWidth(title,titleHeight))/2,428+bottomTextOffset,title,titleHeight,browserTextColor(white));
    if(showGameCount_){
        const std::string gameCount=std::to_string(chosen+1)+" / "+std::to_string(entryCount);
        draw::text(renderer_,8,8+topTextOffset,gameCount,16,browserTextColor(muted));
    }
    if(showBatteryLevel_){
        const std::string batteryLabel=batteryPercent_>=0?std::to_string(batteryPercent_)+"%":"--%";
        draw::text(renderer_,kDesignW-8-draw::textWidth(batteryLabel,16),8+topTextOffset,batteryLabel,16,browserTextColor(muted));
    }

    const Category* selectedCategory=currentCategory();
    const std::string listHeader=selectedCategory?selectedCategory->label:"";
    const int listHeaderHeight=26;
    draw::text(renderer_,(kDesignW-draw::textWidth(listHeader,listHeaderHeight))/2,48+topTextOffset,listHeader,listHeaderHeight,browserTextColor(white));
    const std::string systemHeader=entries[static_cast<size_t>(chosen)].system;
    const int systemHeaderHeight=18;
    // A normal console list already names its system as the main header. Keep
    // the secondary system label for named collections and Favourite, where
    // it adds context instead of repeating the same heading.
    if(lower(systemHeader)!=lower(listHeader))
        draw::text(renderer_,(kDesignW-draw::textWidth(systemHeader,systemHeaderHeight))/2,75+topTextOffset,systemHeader,systemHeaderHeight,browserTextColor(muted));
    const size_t upCategoryIndex=adjacentVisibleCategory(-1);
    const size_t downCategoryIndex=adjacentVisibleCategory(1);
    if(upCategoryIndex!=categoryIndex_ || downCategoryIndex!=categoryIndex_){
        const Category& upCategory=categories_[upCategoryIndex];
        const Category& downCategory=categories_[downCategoryIndex];
        const SDL_Color subheader=browserTextColor(SDL_Color{145,148,154,255});
        const std::string upLabel="UP: "+shortLabel(upCategory.label,32);
        const int upW=draw::textWidth(upLabel,16);
        arrow(renderer_,kDesignW/2,8+topTextOffset,true,subheader);
        draw::text(renderer_,(kDesignW-upW)/2,22+topTextOffset,upLabel,16,subheader);
        const std::string downLabel="DOWN: "+shortLabel(downCategory.label,31);
        const int downW=draw::textWidth(downLabel,16);
        draw::text(renderer_,(kDesignW-downW)/2,477+bottomTextOffset,downLabel,16,subheader);
        arrow(renderer_,kDesignW/2,501+bottomTextOffset,false,subheader);
    }
    if(showControlHints_){
        const std::string hints="UP/DOWN LIST   LEFT/RIGHT GAME   A OPEN   X REFRESH   SELECT OPTIONS   Y SEARCH";
        draw::text(renderer_,(kDesignW-draw::textWidth(hints,15))/2,532+bottomTextOffset,hints,15,browserTextColor(SDL_Color{150,154,162,255}));
    }
    if(launchAnimation_ && launchAwaitingConfirm_){
        const Game* preview=launchingGame();
        if(preview){
            std::string info=launchArtworkLoading_?"LOADING IMAGES":"NO ARTWORK VARIANTS";
            if(!launchArtworkLoading_ && previewShowingDetails_){
                info="GAME DETAILS  "+std::to_string(preview->artVariants.size()+1)+" / "+
                     std::to_string(preview->artVariants.size()+1);
            }else if(!launchArtworkLoading_ && !preview->artVariants.empty()){
                const std::string artworkLabel=shortLabel(filenameWithoutExtension(preview->artPath),28);
                info="IMAGE "+std::to_string(preview->artSelection+1)+" / "+
                     std::to_string(preview->artVariants.size())+"   "+artworkLabel;
            }
            if(!launchArtworkLoading_ && previewShowingDetails_) drawPreviewGameDetails(*preview);
            draw::text(renderer_,(kDesignW-draw::textWidth(info,15))/2,469,info,15,browserTextColor(muted));
            const std::string hints="LEFT/RIGHT BROWSE   A LAUNCH   B BACK";
            draw::text(renderer_,(kDesignW-draw::textWidth(hints,15))/2,528,hints,15,browserTextColor(SDL_Color{150,154,162,255}));
        }
    }

    if(overlay_!=Overlay::None){
        SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
        fillRect(renderer_,SDL_Rect{58,85,636,390},{4,7,15,238});
        outlineRect(renderer_,SDL_Rect{58,85,636,390},{70,82,100,255});
        const SDL_Color accent{135,205,255,255};
        if(overlay_==Overlay::SelectMenu || overlay_==Overlay::AddToList){
            const bool listScreen=overlay_==Overlay::AddToList;
            const std::string heading=listScreen?"ADD TO LIST":"GAME OPTIONS";
            draw::text(renderer_,(kDesignW-draw::textWidth(heading,27))/2,105,heading,27,white);
            const std::vector<std::string>& options=listScreen?listOptions_:menuOptions_;
            if(options.empty()){
                const std::string none="NO LISTS FOUND";
                draw::text(renderer_,(kDesignW-draw::textWidth(none,23))/2,245,none,23,muted);
            }else{
                const int rowHeight=36;
                const int visibleRows=std::min(8,static_cast<int>(options.size()));
                int start=std::max(0,overlaySelection_-visibleRows+1);
                for(int row=0;row<visibleRows;++row){
                    const int index=start+row;
                    const int y=158+row*rowHeight;
                    if(index==overlaySelection_) fillRect(renderer_,SDL_Rect{90,y-4,572,32},{32,50,72,255});
                    const std::string item=(index==overlaySelection_?"> ":"  ")+options[static_cast<size_t>(index)];
                    draw::text(renderer_,112,y,item,21,index==overlaySelection_?accent:white);
                }
            }
            const std::string footer="UP/DOWN CHOOSE   A SELECT   B BACK";
            draw::text(renderer_,(kDesignW-draw::textWidth(footer,15))/2,434,footer,15,muted);
        }else if(overlay_==Overlay::Settings){
            std::vector<std::string> names{
                "Carousel spacing","Animation speed","Startup animation","Game title size",
                "Show game counter","Show battery level","Show control hints",
                "Wrap game browsing","Invert text colour","Hide game title",
                "Background","Reset settings"
            };
            std::vector<std::string> values{
                carouselSpacing_==0?"COMPACT":(carouselSpacing_==2?"WIDE":"NORMAL"),
                animationSpeed_==0?"SLOW":(animationSpeed_==2?"FAST":"NORMAL"),
                startupAnimationEnabled_?"ON":"OFF",
                titleSize_==0?"SMALL":(titleSize_==2?"LARGE":"NORMAL"),
                showGameCount_?"ON":"OFF",showBatteryLevel_?"ON":"OFF",
                showControlHints_?"ON":"OFF",wrapGameBrowsing_?"ON":"OFF",
                invertTextColour_?"ON":"OFF",hideGameTitle_?"ON":"OFF",
                backgroundSelection_>=0 && backgroundSelection_<static_cast<int>(backgroundChoices_.size())
                    ?backgroundChoices_[static_cast<size_t>(backgroundSelection_)].label:"NONE",
                "RESTORE DEFAULTS"
            };
            // Keep per-system sizing together at the end so that systems
            // added by OnionOS do not push the everyday options downscreen.
            for(const SystemInfo& system:systems_){
                names.push_back(system.label+" cartridge size");
                const int setting=cartridgeScaleFor(system.id);
                values.push_back(setting==0?"SMALL":(setting==2?"LARGE":"NORMAL"));
                names.push_back(system.label+" visibility");
                values.push_back(systemHiddenFor(system.id)?"HIDDEN":"SHOWN");
            }
            draw::text(renderer_,(kDesignW-draw::textWidth("CARTOS SETTINGS",24))/2,100,"CARTOS SETTINGS",24,white);
            const int first=settingsScrollOffset_;
            const int last=std::min(static_cast<int>(names.size()),first+8);
            for(int i=first;i<last;++i){
                const int row=i-first;
                const int y=139+row*34;
                if(i==settingsSelection_) fillRect(renderer_,SDL_Rect{82,y-2,588,30},{32,50,72,255});
                draw::text(renderer_,94,y+4,(i==settingsSelection_?"> ":"  ")+names[static_cast<size_t>(i)],16,i==settingsSelection_?accent:white);
                const int valueWidth=draw::textWidth(values[static_cast<size_t>(i)],16);
                draw::text(renderer_,650-valueWidth,y+4,values[static_cast<size_t>(i)],16,i==settingsSelection_?accent:muted);
            }
            const std::string footer="UP/DOWN SCROLL   LEFT/RIGHT CHANGE   A SELECT   X REFRESH   B CLOSE";
            draw::text(renderer_,(kDesignW-draw::textWidth(footer,14))/2,435,footer,14,muted);
            const std::string version="CARTOS v" CARTOS_VERSION;
            draw::text(renderer_,(kDesignW-draw::textWidth(version,13))/2,456,version,13,muted);
        }else if(overlay_==Overlay::CreateList){
            const std::string heading="CREATE NEW LIST";
            draw::text(renderer_,(kDesignW-draw::textWidth(heading,27))/2,105,heading,27,white);
            const std::string name=newListName_.empty()?"TYPE A NAME":"NAME: "+newListName_;
            draw::text(renderer_,(kDesignW-draw::textWidth(name,23))/2,153,name,23,accent);
            static const std::string keys[]={"ABCDEFGHIJ","KLMNOPQRST","UVWXYZ0123","456789 -  "};
            for(int row=0;row<4;++row){
                for(int col=0;col<10;++col){
                    const int index=row*10+col;
                    const SDL_Rect keyRect{116+col*53,204+row*42,45,32};
                    if(index==virtualKey_) fillRect(renderer_,keyRect,{41,70,98,255});
                    outlineRect(renderer_,keyRect,index==virtualKey_?accent:SDL_Color{55,63,76,255});
                    const char letter=keys[row][static_cast<size_t>(col)];
                    const std::string caption=letter==' '?"·":std::string(1,letter);
                    draw::text(renderer_,keyRect.x+(keyRect.w-draw::textWidth(caption,17))/2,keyRect.y+7,caption,17,index==virtualKey_?accent:white);
                }
            }
            const std::string footer="A ADD   X BACKSPACE   START CREATE   B CANCEL";
            draw::text(renderer_,(kDesignW-draw::textWidth(footer,15))/2,425,footer,15,muted);
        }else if(overlay_==Overlay::DeleteConfirm){
            const Category* selected=currentCategory();
            const std::string title=selected && !selected->entries.empty()?selected->entries[static_cast<size_t>(wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(selected->entries.size())))].title:"this game";
            draw::text(renderer_,(kDesignW-draw::textWidth("DELETE ROM?",28))/2,135,"DELETE ROM?",28,white);
            const std::string prompt=shortLabel(title,44);
            draw::text(renderer_,(kDesignW-draw::textWidth(prompt,19))/2,196,prompt,19,accent);
            draw::text(renderer_,(kDesignW-draw::textWidth("This removes the ROM only. Saves are kept.",17))/2,238,"This removes the ROM only. Saves are kept.",17,muted);
            const std::array<std::string,2> choices{{"DELETE GAME","CANCEL"}};
            for(int i=0;i<2;++i){
                const SDL_Rect row{202,300+i*48,348,38};
                if(i==overlaySelection_) fillRect(renderer_,row,{32,50,72,255});
                draw::text(renderer_,(kDesignW-draw::textWidth(choices[static_cast<size_t>(i)],21))/2,row.y+6,choices[static_cast<size_t>(i)],21,i==overlaySelection_?accent:white);
            }
            draw::text(renderer_,(kDesignW-draw::textWidth("UP/DOWN CHOOSE   A CONFIRM   B CANCEL",15))/2,420,"UP/DOWN CHOOSE   A CONFIRM   B CANCEL",15,muted);
        }else if(overlay_==Overlay::DeleteListConfirm){
            const std::string heading="DELETE LIST?";
            draw::text(renderer_,(kDesignW-draw::textWidth(heading,28))/2,135,heading,28,white);
            const std::string label=currentCategory()?currentCategory()->label:"";
            draw::text(renderer_,(kDesignW-draw::textWidth(shortLabel(label,36),20))/2,193,shortLabel(label,36),20,accent);
            const std::string note="Games stay on the SD card.";
            draw::text(renderer_,(kDesignW-draw::textWidth(note,17))/2,231,note,17,muted);
            const std::array<std::string,2> choices{{"DELETE LIST","CANCEL"}};
            for(int i=0;i<2;++i){
                const SDL_Rect row{202,292+i*48,348,38};
                if(i==overlaySelection_) fillRect(renderer_,row,{32,50,72,255});
                draw::text(renderer_,(kDesignW-draw::textWidth(choices[static_cast<size_t>(i)],21))/2,row.y+6,choices[static_cast<size_t>(i)],21,i==overlaySelection_?accent:white);
            }
            const std::string footer="UP/DOWN CHOOSE   A CONFIRM   B CANCEL";
            draw::text(renderer_,(kDesignW-draw::textWidth(footer,15))/2,420,footer,15,muted);
        }else if(overlay_==Overlay::GameInfo){
            const Category* selected=currentCategory();
            if(selected && !selected->entries.empty()){
                const Game& game=selected->entries[static_cast<size_t>(wrapIndex(static_cast<int>(std::lround(position_)),static_cast<int>(selected->entries.size())))];
                draw::text(renderer_,(kDesignW-draw::textWidth("GAME DETAILS",27))/2,112,"GAME DETAILS",27,white);
                draw::text(renderer_,92,156,"TITLE",16,muted);
                draw::text(renderer_,92,180,shortLabel(game.title,46),20,white);
                draw::text(renderer_,92,229,"SYSTEM",16,muted);
                draw::text(renderer_,92,253,game.system,20,white);
                draw::text(renderer_,92,302,"PLAY TIME",16,muted);
                const bool activityTrackerInstalled=access((sdRoot_+"/App/PlayActivity/launch.sh").c_str(),R_OK)==0;
                const long long playedSeconds=activityTrackerInstalled?playtimeSecondsFor(game):-1;
                std::string playtime="Install Activity Tracker to see your playtime";
                if(activityTrackerInstalled) playtime="No playtime";
                if(activityTrackerInstalled && playedSeconds>=3600){
                    playtime=std::to_string(playedSeconds/3600)+"h "+std::to_string((playedSeconds%3600)/60)+"m";
                }else if(activityTrackerInstalled && playedSeconds>=60){
                    playtime=std::to_string(playedSeconds/60)+"m";
                }else if(activityTrackerInstalled && playedSeconds>=0){
                    playtime=std::to_string(playedSeconds)+"s";
                }
                const int playtimeSize=activityTrackerInstalled?20:16;
                draw::text(renderer_,92,326,playtime,playtimeSize,activityTrackerInstalled?white:muted);
                draw::text(renderer_,92,376,"ROM FILE",16,muted);
                draw::text(renderer_,92,400,shortLabel(game.romPath,62),16,white);
                const std::string footer="B CLOSE";
                draw::text(renderer_,(kDesignW-draw::textWidth(footer,16))/2,448,footer,16,muted);
            }
        }
        SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
    }
    if(!statusMessage_.empty() && SDL_GetTicks64()<statusUntil_){
        const int width=draw::textWidth(statusMessage_,16);
        fillRect(renderer_,SDL_Rect{(kDesignW-width)/2-8,487,width+16,25},{15,24,36,255});
        draw::text(renderer_,(kDesignW-width)/2,490,statusMessage_,16,{160,216,170,255});
    }
    SDL_RenderPresent(renderer_);
}

void Launcher::renderSearch(){
    renderBackground();
    if(launchAnimation_){
        const Game* selected=nullptr;
        if(!selectedInSearch(selected)){
            SDL_RenderPresent(renderer_);
            return;
        }
        const float animationTime=std::max(0.0f,launchAnimationElapsed_-launchCenterDuration_);
        const float grow=std::min(1.0f,animationTime/0.38f);
        const float growEase=1.0f-std::pow(1.0f-grow,3.0f);
        const float lift=std::max(0.0f,std::min(1.0f,(animationTime-0.50f)/0.14f));
        const float liftEase=lift*lift*(3.0f-2.0f*lift);
        const float drop=std::max(0.0f,std::min(1.0f,(animationTime-0.64f)/0.36f));
        const float dropEase=drop*drop*(3.0f-2.0f*drop);
        const float textProgress=std::min(1.0f,animationTime/0.40f);
        const float textEase=textProgress*textProgress*(3.0f-2.0f*textProgress);
        const float reverseProgress=std::min(1.0f,launchReverseElapsed_/kLaunchReverseDuration);
        const float reverseEase=reverseProgress*reverseProgress*(3.0f-2.0f*reverseProgress);
        const float visibleTextEase=launchReversing_?1.0f-reverseEase:textEase;
        const int topOffset=-190*static_cast<int>(visibleTextEase*1000.0f)/1000;
        const int centerX=kDesignW/2;
        const int consoleScale=cartridgeScaleFor(selected->console);
        const float preference=consoleScale==0?0.90f:(consoleScale==2?1.10f:1.0f);
        const int count=static_cast<int>(searchResults_.size());
        for(int offset=-1;offset<=1;++offset){
            if(count==0) break;
            const int index=wrapIndex(searchResultSelection_+offset,count);
            const auto ref=searchResults_[static_cast<size_t>(index)];
            Game& game=categories_[ref.first].entries[ref.second];
            if(!game.artwork && !game.artPath.empty()) game.artwork=artwork::loadPng(renderer_,game.artPath);
            if(offset==0){
                const float reverseProgress=std::min(1.0f,launchReverseElapsed_/kLaunchReverseDuration);
                const float reverseEase=reverseProgress*reverseProgress*(3.0f-2.0f*reverseProgress);
                const float launchScale=launchReversing_?launchReverseStartScale_+(1.0f-launchReverseStartScale_)*reverseEase:1.0f+0.34f*growEase;
                const float scale=(280.0f/220.0f)*launchScale*preference;
                const int width=static_cast<int>(220.0f*scale);
                const int height=static_cast<int>(275.0f*scale);
                const int liftedCenterY=274-static_cast<int>(30.0f*liftEase);
                const int offscreenCenterY=kDesignH+height/2+30;
                const int centerY=liftedCenterY+static_cast<int>((offscreenCenterY-liftedCenterY)*dropEase);
                const SDL_Rect box{centerX-width/2,centerY-height/2,width,height};
                if(!(launchAwaitingConfirm_ && previewShowingDetails_))
                    draw::cartridge(renderer_,box,game.system,game.title,game.variant,true,game.artwork);
            }else{
                const float slide=std::max(0.0f,std::min(1.0f,animationTime/0.48f));
                const float reverseProgress=std::min(1.0f,launchReverseElapsed_/kLaunchReverseDuration);
                const float reverseEase=reverseProgress*reverseProgress*(3.0f-2.0f*reverseProgress);
                const float slideEase=launchReversing_?1.0f-reverseEase:1.0f-std::pow(1.0f-slide,3.0f);
                const int width=112;
                const int height=140;
                const int x=centerX+offset*176+static_cast<int>(offset*slideEase*500.0f)-width/2;
                const SDL_Rect box{x,274-height/2,width,height};
                draw::cartridge(renderer_,box,game.system,game.title,game.variant,false,game.artwork);
            }
        }
        const std::string heading="SEARCH GAMES";
        draw::text(renderer_,(kDesignW-draw::textWidth(heading,27))/2,45+topOffset,heading,27,white);
        const std::string query="\""+shortLabel(searchQuery_,44)+"\"";
        draw::text(renderer_,(kDesignW-draw::textWidth(query,22))/2,86+topOffset,query,22,{145,205,255,255});
        // The enlarged-cartridge view is intentionally artwork-only. The
        // title and system/count labels are omitted once the launch animation
        // starts instead of lingering at the bottom edge.
        if(launchAwaitingConfirm_){
            std::string info=launchArtworkLoading_?"LOADING IMAGES":"NO ARTWORK VARIANTS";
            if(!launchArtworkLoading_ && previewShowingDetails_){
                info="GAME DETAILS  "+std::to_string(selected->artVariants.size()+1)+" / "+
                     std::to_string(selected->artVariants.size()+1);
                drawPreviewGameDetails(*selected);
            }else if(!launchArtworkLoading_ && !selected->artVariants.empty()){
                const std::string artworkLabel=shortLabel(filenameWithoutExtension(selected->artPath),28);
                info="IMAGE "+std::to_string(selected->artSelection+1)+" / "+
                     std::to_string(selected->artVariants.size())+"   "+artworkLabel;
            }
            draw::text(renderer_,(kDesignW-draw::textWidth(info,15))/2,469,info,15,muted);
            const std::string hints="LEFT/RIGHT BROWSE   A LAUNCH   B BACK";
            draw::text(renderer_,(kDesignW-draw::textWidth(hints,15))/2,528,hints,15,muted);
        }
        SDL_RenderPresent(renderer_);
        return;
    }
    const std::string heading="SEARCH GAMES";
    draw::text(renderer_,(kDesignW-draw::textWidth(heading,27))/2,45,heading,27,white);
    const std::string query=searchQuery_.empty()?"TYPE TO SEARCH":"\""+shortLabel(searchQuery_,44)+"\"";
    draw::text(renderer_,(kDesignW-draw::textWidth(query,22))/2,86,query,22,{145,205,255,255});
    const Game* selected=nullptr;
    const bool hasSelected=selectedInSearch(selected);
    if(hasSelected){
        const int count=static_cast<int>(searchResults_.size());
        for(int offset=-1;offset<=1;++offset){
            if(count==0) break;
            const int index=wrapIndex(searchResultSelection_+offset,count);
            const auto neighborRef=searchResults_[static_cast<size_t>(index)];
            Game& game=categories_[neighborRef.first].entries[neighborRef.second];
            if(!game.artwork && !game.artPath.empty()) game.artwork=artwork::loadPng(renderer_,game.artPath);
            const int width=offset==0?220:96;
            const int height=offset==0?275:120;
            const int x=kDesignW/2+offset*(offset==0?0:150)-width/2;
            const int carouselCenterY=searchResultsFocused_?238:274;
            const SDL_Rect box{x,carouselCenterY-height/2,width,height};
            // Search already draws the ROM title beneath the carousel. Keep
            // fallback shells free of a second, overlapping copy of the name.
            draw::cartridge(renderer_,box,game.system,"",game.variant,offset==0,game.artwork);
        }
        const std::string title=shortLabel(selected->title,52);
        const int titleY=searchResultsFocused_?397:329;
        const int subtitleY=searchResultsFocused_?428:360;
        draw::text(renderer_,(kDesignW-draw::textWidth(title,22))/2,titleY,title,22,white);
        const std::string system=selected->system+"   "+std::to_string(searchResultSelection_+1)+" / "+std::to_string(searchResults_.size());
        draw::text(renderer_,(kDesignW-draw::textWidth(system,16))/2,subtitleY,system,16,muted);
    }else{
        const std::string none=searchQuery_.empty()?"START TYPING TO FIND A GAME":"NO MATCHES";
        draw::text(renderer_,(kDesignW-draw::textWidth(none,20))/2,245,none,20,muted);
    }
    if(!searchResultsFocused_){
        for(int row=0;row<4;++row){
            for(int col=0;col<10;++col){
                const int index=row*10+col;
                const SDL_Rect keyRect{111+col*55,398+row*28,47,23};
                if(index==virtualKey_)fillRect(renderer_,keyRect,{39,64,90,255});
                outlineRect(renderer_,keyRect,index==virtualKey_?SDL_Color{145,205,255,255}:SDL_Color{54,62,75,255});
                const char value=searchKeyboardCharacter(index);
                const std::string label=value==' '?"SPACE":std::string(1,value);
                const int labelSize=value==' '?12:15;
                draw::text(renderer_,keyRect.x+(keyRect.w-draw::textWidth(label,labelSize))/2,keyRect.y+4,label,labelSize,index==virtualKey_?SDL_Color{145,205,255,255}:white);
            }
        }
        const std::string footer="A TYPE   B BACKSPACE   X REFRESH   START RESULTS";
        draw::text(renderer_,(kDesignW-draw::textWidth(footer,14))/2,520,footer,14,muted);
    }else{
        const std::string footer="LEFT/RIGHT BROWSE   A OPEN   Y TOGGLE RESULTS   B KEYBOARD";
        draw::text(renderer_,(kDesignW-draw::textWidth(footer,14))/2,520,footer,14,muted);
    }
    SDL_RenderPresent(renderer_);
}

int Launcher::run(){
    if(!init()){shutdown();return 1;}
    Uint64 previous=SDL_GetPerformanceCounter();
    while(running_){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            bool skipStartup=e.type==SDL_KEYDOWN || e.type==SDL_CONTROLLERBUTTONDOWN ||
                             e.type==SDL_JOYBUTTONDOWN;
            if(e.type==SDL_JOYHATMOTION && e.jhat.value!=SDL_HAT_CENTERED)
                skipStartup=true;
            if(e.type==SDL_JOYAXISMOTION && (e.jaxis.value>8000 || e.jaxis.value< -8000))
                skipStartup=true;
            if(e.type==SDL_CONTROLLERAXISMOTION && (e.caxis.value>8000 || e.caxis.value< -8000))
                skipStartup=true;
            if(skipStartup) startupAnimation_=false;
            handle(e);
        }
        updateHeldScroll(SDL_GetTicks64());
        const Uint64 now=SDL_GetPerformanceCounter();
        float dt=static_cast<float>(now-previous)/static_cast<float>(SDL_GetPerformanceFrequency());
        previous=now; dt=std::min(dt,0.05f);
        if(startupAnimation_){
            startupAnimationElapsed_+=dt;
            if(startupAnimationElapsed_>=1.20f) startupAnimation_=false;
        }
        if(launchAnimation_ && !launchAwaitingConfirm_){
            const float speed=animationSpeed_==0?0.75f:(animationSpeed_==2?1.35f:1.0f);
            if(launchReversing_){
                launchReverseElapsed_+=dt*speed;
                if(launchReverseElapsed_>=kLaunchReverseDuration){
                    launchReverseElapsed_=kLaunchReverseDuration;
                    launchAnimation_=false;
                    launchReversing_=false;
                    launchStepTwoConfirmed_=false;
                    resetPreviewArtwork();
                }
            }else{
                launchAnimationElapsed_+=dt*speed;
            }
            const float stepOneDuration=launchCenterDuration_+0.50f;
            if(launchAnimation_ && !launchReversing_ && !launchStepTwoConfirmed_ && launchAnimationElapsed_>=stepOneDuration){
                launchAnimationElapsed_=stepOneDuration;
                launchAwaitingConfirm_=true;
                // Defer artwork discovery until the enlarged preview is on
                // screen, then keep its loading message visible while scanning.
                launchArtworkLoading_=true;
            }
        }
        render(dt);
        if(launchArtworkLoading_ && launchAwaitingConfirm_){
            if(Game* game=launchingGame()){
                loadArtworkVariants(*game);
                resetPreviewArtwork();
            }
            launchArtworkLoading_=false;
        }
        if(!rendererVsync_) SDL_Delay(16);
        if(launchAnimation_ && !launchAwaitingConfirm_ && !launchReversing_ && launchAnimationElapsed_>=kLaunchAnimationDuration){
            launchAnimation_=false;
            if(launchAnimationCategory_<categories_.size() && launchAnimationEntry_<categories_[launchAnimationCategory_].entries.size())
                launchGame(categories_[launchAnimationCategory_].entries[launchAnimationEntry_]);
        }
    }
    shutdown();
    return 0;
}
