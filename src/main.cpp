#include "app/app.hpp"
#include <SDL3/SDL_main.h>

#if defined(__ANDROID__)
#include <android/log.h>
#define LOG_TAG "FolioNoteNative"
#define ALOG(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ANDROID: SDLActivity.java loads libmain.so and looks up the symbol "SDL_main"
// using dlsym() at runtime. If the function is compiled as plain C++ without
// extern "C", the compiler applies C++ name mangling (e.g. "_Z8SDL_mainiPPc")
// and dlsym() can't find it — causing an immediate silent exit.
// __attribute__((visibility("default"))) ensures the symbol isn't stripped by
// the linker even if -fvisibility=hidden is set globally in CMakeLists.txt.
extern "C" __attribute__((visibility("default"))) 
int SDL_main(int argc, char* argv[])
#else
int main(int argc, char* argv[])
#endif
{
#if defined(__ANDROID__)
    ALOG("FolioNote started successfully on Android!");
#endif

    Application app;
    if (!app.Init("FolioNote", 1920, 1080)) {
        return -1;
    }

    app.Run();
    app.Shutdown();
    return 0;
}