#include "windows.h"
#include "shellapi.h"

//----------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine,
                     int nCmdShow )
{
    int argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLine(), &argc);
    bool bStartServer = false;
    bool bStopServer = false;

    if (argc > 1) {
        int i = 1;

        while (i < argc) {
            if (wcscmp(argv[i], L"--start") == 0 || wcscmp(argv[i], L"-s") == 0) {
                bStartServer = true;
            } else if (wcscmp(argv[i], L"--stop") == 0 || wcscmp(argv[i], L"-q") == 0) {
                bStopServer = true;
            }
            i++;
        }
    }


    char path[MAX_PATH + 1];
    GetModuleFileNameA( NULL, path, MAX_PATH + 1 );
    char* pos = strrchr(path, '\\');
    if (pos) {
        *pos = '\0';
    }

    if (bStartServer) {
        ShellExecuteA(NULL, "open", "QtdSync.exe", "--server-start", path, SW_HIDE);
    } else if (bStopServer) {
        ShellExecuteA(NULL, "open", "QtdSync.exe", "--server-stop", path, SW_HIDE);        
    } else {
        ShellExecuteA(NULL, "open", "QtdSync.exe", "--server-config", path, SW_SHOW);
    }
    return 0;
}