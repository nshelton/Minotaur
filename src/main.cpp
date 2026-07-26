// Minimal entry that boots the app and renders the main screen
#include "app/App.h"
#include "core/Vec2.h"
#include "screens/MainScreen.h"
#include "filters/FilterRegistry.h"
#include "generators/GeneratorRegistry.h"

int main(int argc, char *argv[]) {
    FilterRegistry::initDefaults();
    GeneratorRegistry::initDefaults();
    App app(2200, 1600, "Minotaur");
    // Empty path -> MainScreen resolves to the standard projects directory
    std::string projectPath = (argc > 1) ? argv[1] : "";
    MainScreen screen(projectPath);
    app.run(screen);
    return 0;
}


