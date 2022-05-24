#include "windows.h"
#include "shellapi.h"

//----------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine,
                     int nCmdShow )
{
    char path[MAX_PATH + 1];
    GetModuleFileNameA( NULL, path, MAX_PATH + 1 );
    char* pos = strrchr(path, '\\');
    if (pos) {
        *pos = '\0';
    }

    ShellExecuteA(NULL, "open", "QtdSync.exe", "--monitor", path, SW_SHOW);
    return 0;
}