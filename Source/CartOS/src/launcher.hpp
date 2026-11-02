#pragma once
#include <SDL.h>
#include "platform.hpp"
#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

enum class EntryKind { Game, App };

struct Game {
    EntryKind kind=EntryKind::Game;
    std::string title;
    std::string system;
    std::string console;
    std::string romPath;
    std::string artPath;
    std::string defaultArtPath;
    std::vector<std::string> artVariants;
    std::string launcherPath;
    std::string workingDirectory;
    bool onionMainUi=false;
    bool cartLauncherSettings=false;
    SDL_Texture* artwork=nullptr;
    bool artworkAttempted=false;
    size_t artSelection=0;
    int variant=0;
};

struct Category {
    std::string label;
    std::string systemId;
    std::vector<Game> entries;
};

struct SystemInfo {
    std::string id;
    std::string label;
    std::string launcherPath;
    std::vector<std::string> romPaths;
    std::string artworkPath;
    std::vector<std::string> extensions;
};

struct BackgroundOption {
    std::string filename;
    std::string label;
};

class Launcher {
public:
    Launcher();
    int run();
private:
    enum class Overlay { None, SelectMenu, AddToList, CreateList, DeleteConfirm, DeleteListConfirm, GameInfo, Search, Settings };
    bool init();
    void shutdown();
    void handle(const SDL_Event& e);
    void move(int direction);
    void moveCategory(int direction);
    void setScrollHeld(int direction, bool held);
    void updateHeldScroll(Uint64 now);
    void render(float dt);
    void renderSearch();
    void renderBackground();
    void loadSettings();
    void saveSettings() const;
    void changeSetting(int direction);
    void reloadBackgroundChoices();
    bool selectBackground(int direction);
    void moveSettingsSelection(int direction);
    void discoverRecent(const std::string& sdRoot);
    void discoverSystems(const std::string& sdRoot);
    void discoverGames();
    void discoverCollections(const std::string& sdRoot);
    void discoverApps(const std::string& sdRoot);
    void launchSelected();
    void beginLaunchAnimation(size_t categoryIndex, size_t entryIndex);
    Game* launchingGame();
    void browsePreviewPage(int direction);
    void selectPreviewArtwork(size_t selection);
    void resetPreviewArtwork();
    void loadArtworkVariants(Game& game);
    void drawPreviewGameDetails(const Game& game);
    void launchGame(const Game& game);
    void openOnionGameSwitcher();
    void beginMenuPress();
    void endMenuPress();
    void openSelectMenu();
    void activateSelectMenu();
    void addSelectedToList(const std::string& label);
    void toggleSelectedFavorite();
    void removeSelectedFromList();
    void deleteSelectedGame();
    void createList();
    void deleteSelectedList();
    void refreshAll();
    void startSearch();
    void updateSearchResults();
    void launchSearchSelected();
    void handleSearchInput(const SDL_Event& e);
    bool writeListEntry(const std::string& path, const Game& game, bool removeExisting);
    bool removeListEntry(const std::string& path, const Game& game);
    std::string collectionPath(const std::string& label) const;
    std::vector<std::string> customListNames() const;
    bool isSelectedFavorite() const;
    bool selectedInSearch(const Game*& game) const;
    long long playtimeSecondsFor(const Game& game);
    Category* currentCategory();
    const Category* currentCategory() const;
    size_t adjacentVisibleCategory(int direction) const;
    const SystemInfo* systemForLauncher(const std::string& launcher) const;
    const SystemInfo* systemForRomPath(const std::string& romPath) const;
    int cartridgeScaleFor(const std::string& systemId) const;
    bool systemHiddenFor(const std::string& systemId) const;
    int settingsRowCount() const;
    Platform platform_;
    SDL_Window* window_=nullptr;
    SDL_Renderer* renderer_=nullptr;
    bool rendererVsync_=false;
    SDL_Texture* backgroundArtwork_=nullptr;
    std::vector<BackgroundOption> backgroundChoices_;
    std::string backgroundFile_="BG.png";
    int backgroundSelection_=0;
    SDL_GameController* controller_=nullptr;
    SDL_Joystick* joystick_=nullptr;
    std::vector<Category> categories_;
    std::vector<SystemInfo> systems_;
    std::vector<int> rememberedSelections_;
    std::unordered_map<std::string,std::vector<std::string>> artworkDirectoryCache_;
    std::string sdRoot_;
    std::string playtimeLookupRom_;
    long long playtimeLookupSeconds_=-1;
    std::array<bool,4> scrollHeld_{{false,false,false,false}};
    std::array<Uint64,4> scrollRepeatAt_{{0,0,0,0}};
    std::array<bool,4> scrollSettingsMode_{{false,false,false,false}};
    bool showGameCount_=true;
    bool showBatteryLevel_=true;
    bool showControlHints_=true;
    bool invertTextColour_=false;
    bool hideGameTitle_=false;
    std::unordered_map<std::string,int> cartridgeScaleBySystem_;
    std::unordered_map<std::string,bool> hiddenSystemById_;
    int carouselSpacing_=1;
    int animationSpeed_=1;
    int titleSize_=1;
    bool wrapGameBrowsing_=true;
    int batteryPercent_=-1;
    Uint32 batteryPollAt_=0;
    Overlay overlay_=Overlay::None;
    std::vector<std::string> menuOptions_;
    std::vector<std::string> listOptions_;
    int overlaySelection_=0;
    std::string statusMessage_;
    Uint64 statusUntil_=0;
    std::string newListName_;
    int virtualKey_=0;
    bool searchResultsFocused_=false;
    std::string searchQuery_;
    std::vector<std::pair<size_t,size_t>> searchResults_;
    int searchResultSelection_=0;
    bool menuButtonHeld_=false;
    bool menuButtonChord_=false;
    bool launchAnimation_=false;
    bool launchArtworkLoading_=false;
    bool startupAnimationEnabled_=true;
    bool startupAnimation_=false;
    float startupAnimationElapsed_=0.0f;
    bool launchAwaitingConfirm_=false;
    bool previewShowingDetails_=false;
    bool launchReversing_=false;
    bool launchStepTwoConfirmed_=false;
    size_t launchAnimationEntry_=0;
    size_t launchAnimationCategory_=0;
    float launchAnimationElapsed_=0.0f;
    float launchCenteringOffset_=0.0f;
    float launchCenteringPosition_=0.0f;
    float launchCenterDuration_=0.16f;
    float launchReverseElapsed_=0.0f;
    float launchReverseStartScale_=1.34f;
    Uint64 launchFirstPressAt_=0;
    int settingsSelection_=0;
    int settingsScrollOffset_=0;
    size_t categoryIndex_=0;
    float position_=0.0f;
    float target_=0.0f;
    bool running_=true;
};
