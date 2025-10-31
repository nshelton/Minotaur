// Minimal entry that boots the app and renders the main screen
#include "app/App.h"
#include "core/Vec2.h"
#include "screens/MainScreen.h"

int main() {
    App app(800, 600, "Minotaur");
    MainScreen screen;
    app.run(screen);
    return 0;
}


