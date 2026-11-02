#pragma once
#include <string>

// The OnionOS device CartOS runs on, detected once at startup with the same
// sources OnionOS itself uses. Every value can be overridden from launch.sh.
struct Platform {
    std::string id;      // "miyoo-mini", "miyoo-mini-plus" or "desktop"
    std::string name;    // human readable, for logs
    std::string sdRoot;  // CARTOS_SD_ROOT, default /mnt/SDCARD
    int screenW=0;       // physical panel size; 0 when unknown
    int screenH=0;
};

namespace platform {
Platform detect();
// Battery percentage 0-100, or -1 when it is not available.
int batteryPercent();
}
