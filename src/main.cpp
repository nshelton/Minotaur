// Minimal entry that boots the app and renders the main screen
#include "app/App.h"
#include "core/Vec2.h"
#include "screens/MainScreen.h"
#include "filters/FilterRegistry.h"

int main() {
    FilterRegistry::initDefaults();
    App app(1800, 1600, "Minotaur");
    MainScreen screen;
    app.run(screen);
    return 0;
}


