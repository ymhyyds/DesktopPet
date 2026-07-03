#include <cstdio>
#include <windows.h>

// This gets linked first - simple check
BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

// Force this to be called at startup
static struct StartupLogger {
    StartupLogger() {
        FILE *f = fopen("C:\\Users\\余敏涵\\Desktop\\DesktopPet\\build\\Release\\startup_log.txt", "w");
        if (f) {
            fprintf(f, "StartupLogger constructor called\n");
            fclose(f);
        }
    }
} g_logger;
