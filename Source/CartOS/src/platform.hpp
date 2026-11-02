#pragma once
#include <string>

// Everything CartOS needs to know about the handheld it runs on, detected once
// at startup. Like OnionOS and spruceOS, detection is file based and every
// value can be overridden from launch.sh with an environment variable.
struct Platform {
    std::string id;      // e.g. "miyoo-mini", "miyoo-mini-plus", "desktop"
    std::string name;    // human readable, for logs
    std::string sdRoot;  // CARTOS_SD_ROOT, default /mnt/SDCARD
    int screenW=0;       // native panel size; 0 when unknown
    int screenH=0;
};

namespace platform {
Platform detect();
// Battery percentage 0-100, or -1 when the device does not report it.
int batteryPercent();
}
