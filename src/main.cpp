// Minimal entry that boots the app and renders the main screen
#include "app/App.h"
#include "core/Vec2.h"
#include "screens/MainScreen.h"
#include "filters/FilterRegistry.h"

int main(int argc, char *argv[]) {
    FilterRegistry::initDefaults();
    App app(2200, 1600, "Minotaur");
    std::string projectPath = (argc > 1) ? argv[1] : "page.json";
    MainScreen screen(projectPath);
    app.run(screen);
    return 0;
}


