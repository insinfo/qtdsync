#include "QtdTools.h"

#include "QtdBase.h"

#ifdef WIN32
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")
#endif

#ifndef WIN32
#include<sys/stat.h>
#ifdef Q_WS_X11
#include <sys/vfs.h>
#endif

//----------------------------------------------------------------------------
int file_exists (char * fileName)
{
   struct stat buf;
   int i = stat ( fileName, &buf );
     /* File found */
     if ( i == 0 )
     {
       return 1;
     }
     return 0;
       
}

//----------------------------------------------------------------------------
int ShellExecuteA(int* pParent, char* cmd, char* file, char* args, char* dir, int nType)
{
    char* pSystem = 0L;
    int ret;

    if (strcmp(cmd, "runas") == 0) {
        char* sudo = "/usr/bin/kdesudo";
        if (!file_exists(sudo)) {
            sudo = "usr/bin/gksudo";
        }
        pSystem =  new char[strlen(sudo) + 3 + strlen(file) + strlen(args) + 4];
        sprintf(pSystem, "%s \"%s %s\" &", sudo, file, args);
    } else {
        pSystem = new char[strlen(file) + strlen(args) + 4];
        sprintf(pSystem, "%s %s &", file, args);
    }
    ret = system(pSystem);

    delete [] pSystem;
    return ret;
}
#endif

//----------------------------------------------------------------------------
QStringList QtdTools::getCommandLineArguments(QString args)
{
    QStringList argList = args.split(" ", QString::SkipEmptyParts);
    QStringList newArgs;
    QString arg;
    while (argList.count() > 0) {
        arg += argList.takeFirst();
        if (arg.count("\"") % 2 == 0) {
            if (arg.startsWith("\"") && arg.endsWith("\"")) {
                arg = arg.mid(1, arg.length()-2);
            }
            newArgs << arg;
            arg = "";
        } else {
            arg += " ";
        }
    }
    if (arg != "") {
        if (arg.startsWith("\"") && arg.endsWith("\"")) {
            arg = arg.mid(1, arg.length()-2);
        }
        newArgs << arg;
    }
    return newArgs;
}

//----------------------------------------------------------------------------
QString QtdTools::codecSaveString(QString input)
{
    QString returnStr(input);
    if (input.startsWith("base64;")) {

        QString     base64  = input.mid(7);
        QByteArray  ba      = QByteArray::fromBase64(base64.toLatin1());
        ushort*     pStr    =  (ushort*)ba.data();
        
        returnStr.setUtf16(pStr, ba.count()/2);

    } else {
        if (input != QString(input.toUtf8())) {
            ushort*     pStr    = (ushort*)input.utf16();
            QByteArray  ba((const char*)pStr, input.length()*2);
            returnStr   = "base64;" + QString(ba.toBase64());
        }
    }

    return returnStr;
}

//----------------------------------------------------------------------------
QtdTools::DriveType QtdTools::getDriveType(char letter)
{
    QtdTools::DriveType eType = eDrive_Unknown;
#ifdef WIN32
    HANDLE h;
    TCHAR tsz[8];

    letter -= 'A' - 1;

    wsprintf(tsz, TEXT("\\\\.\\%c:"), TEXT('@') + letter);
    h = CreateFile(tsz, 0, FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
    if (h != INVALID_HANDLE_VALUE) {
        DISK_GEOMETRY Geom[20];
        DWORD cb;

        if (DeviceIoControl (h, IOCTL_DISK_GET_MEDIA_TYPES, 0, 0, Geom, sizeof(Geom), &cb, 0) && cb > 0) {
            switch (Geom[0].MediaType) {
                case F5_1Pt2_512: // 5.25 1.2MB floppy
                case F5_360_512:  // 5.25 360K  floppy
                case F5_320_512:  // 5.25 320K  floppy
                case F5_320_1024: // 5.25 320K  floppy
                case F5_180_512:  // 5.25 180K  floppy
                case F5_160_512:  // 5.25 160K  floppy
                case F3_1Pt44_512: // 3.5 1.44MB floppy
                case F3_2Pt88_512: // 3.5 2.88MB floppy
                case F3_20Pt8_512: // 3.5 20.8MB floppy
                case F3_720_512:   // 3.5 720K   floppy
                    eType = eDrive_Floppy;
                    break;

                case RemovableMedia:
                    eType = eDrive_Removable;
                    break;

                case FixedMedia:
                    eType = eDrive_Fixed;
                    break;

                default:
                    break;
            }
        }

        CloseHandle(h);
    }

    if (eType == eDrive_Unknown) {
        wsprintf(tsz, TEXT("%c:\\"), TEXT('@') +  letter);

        switch (GetDriveType(tsz)) {
            case DRIVE_FIXED:
                eType = eDrive_Fixed;
                break;
            case DRIVE_REMOVABLE:
                eType = eDrive_Removable;
                break;
            case DRIVE_REMOTE:
                eType = eDrive_Network;
                break;
            case DRIVE_CDROM:
                eType = eDrive_CDROM;
                break;
            case DRIVE_RAMDISK:
                eType = eDrive_RamDisk;
                break;
            default:
                break;
        }
    }
#endif
    return eType;
}


#ifndef WIN32
void priv_fillVersionArray(int a, int b, int c, int d, unsigned char version[4])
{
    version[0] = a;
    version[1] = b;
    version[2] = c;
    version[3] = d;
}
#endif

//----------------------------------------------------------------------------
void QtdTools::productInfo(QString name, ProductInfo& info)
{
#ifdef WIN32
    char* fileName = 0L;
    DWORD dwDummy, vMS, vLS;
    DWORD dwFVISize = GetFileVersionInfoSizeA(name.toLatin1().data() , &dwDummy );

    LPBYTE lpVersionInfo = new BYTE[dwFVISize];

    GetFileVersionInfoA(name.toLatin1().data() , 0 , dwFVISize , lpVersionInfo );

    UINT uLen;
    VS_FIXEDFILEINFO *lpFfi;

    VerQueryValueA( lpVersionInfo , "\\" , (LPVOID *)&lpFfi , &uLen );
    VerQueryValueA( lpVersionInfo,  "\\StringFileInfo\\040904b0\\ProductName", (LPVOID*)&fileName, &uLen);
    info.name = QString(fileName);

    VerQueryValueA( lpVersionInfo,  "\\StringFileInfo\\040904b0\\LegalCopyright", (LPVOID*)&fileName, &uLen);
    info.copyright = QString(fileName);

    VerQueryValueA( lpVersionInfo,  "\\StringFileInfo\\040904b0\\Company", (LPVOID*)&fileName, &uLen);
    info.company = QString(fileName);

    VerQueryValueA( lpVersionInfo,  "\\StringFileInfo\\040904b0\\PrivateBuild", (LPVOID*)&fileName, &uLen);
    info.build = QString(fileName);

    VerQueryValueA( lpVersionInfo,  "\\StringFileInfo\\040904b0\\ProductVersion", (LPVOID*)&fileName, &uLen);
    info.versionName = QString(fileName);

    vMS = lpFfi->dwFileVersionMS;
    vLS = lpFfi->dwFileVersionLS;

    delete [] lpVersionInfo;


    info.version[0] = HIWORD(vMS);
    info.version[1] = LOWORD(vMS);
    info.version[2] = HIWORD(vLS);
    info.version[3] = LOWORD(vLS);

#else
#include "version.h"
#define QTD_VERSION_STR(a) #a

    info.name           = QFileInfo(qApp->applicationFilePath()).baseName();
    info.company        = "QtdTools";
    info.versionName    = QTD_NICE_VERSION;
    priv_fillVersionArray(QTD_RC_VERSION, info.version);
    info.copyright      = QString("(c) %1 by Thomas Doering").arg(QDate::currentDate().year());
    info.build          = info.versionName;
#endif
}

#ifdef WIN32
extern Q_CORE_EXPORT int qt_ntfs_permission_lookup;
#else
int qt_ntfs_permission_lookup = 0;
#endif

//----------------------------------------------------------------------------
double QtdTools::freeSpace(QString dir, QtdTools::FreeSpaceType& type)
{
    double dFreeSpace = 0.0;
    type = eFreeSpace_kB;
#ifdef WIN32
    // determin free space
    ULARGE_INTEGER nSpace;

    GetDiskFreeSpaceExA(dir.toLatin1().data(), &nSpace, 0L, 0L);
    dFreeSpace = (double)nSpace.QuadPart / (double)1024;
#elif defined(Q_WS_X11)
    struct statfs s;

    if (statfs(dir.toLatin1().data(), &s) == 0 && s.f_blocks > 0) {
        dFreeSpace = (double)s.f_bavail * (s.f_bsize / 1024.0);
    }
#endif

    if (dFreeSpace > 2048.0) {
        dFreeSpace /= (double)1024;
        type = eFreeSpace_MB;
        if (dFreeSpace > 2048.0) {
            dFreeSpace /= (double)1024;
            type = eFreeSpace_GB;
            if (dFreeSpace > 2048.0) {
                dFreeSpace /= (double)1024;
                type = eFreeSpace_TB;
            }
        }
    }
    return dFreeSpace;
}

//----------------------------------------------------------------------------
bool QtdTools::removeDirectory(QDir aDir)
{
    bool has_err = false;
    if (aDir.exists()) {
        QFileInfoList entries = aDir.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files);
        int count = entries.size();
        for (int idx = 0; idx < count; idx++) {
            QFileInfo entryInfo = entries[idx];
            QString path = entryInfo.absoluteFilePath();
            if (entryInfo.isDir()) {
                has_err = removeDirectory(QDir(path));
            } else {
                qt_ntfs_permission_lookup++;
                QFile file(path);
                file.setPermissions((QFile::Permission)0xFFFF);
                if (!file.remove()) {
                    has_err = true;
                }
                qt_ntfs_permission_lookup--;
            }
        }
        if (!aDir.rmdir(aDir.absolutePath())) {
            has_err = true;
        }
    }
    return(has_err);
}

//----------------------------------------------------------------------------
bool QtdTools::removeDirectory(QString aDir)
{
    return QtdTools::removeDirectory(QDir(aDir));
}

//----------------------------------------------------------------------------
QString QtdTools::readFile(QString fileName)
{
    QFile file(fileName);
    if (!file.exists()) {
        return QString();
    }

    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QString content(QString::fromUtf8(file.readAll()));
    file.close();

    return content;
}

//----------------------------------------------------------------------------
bool QtdTools::writeFile(QString fileName, QString content, bool bAppend)
{
    QFile file(fileName);
    bool bFileOpen = false;
    if (bAppend && file.exists()) {
        bFileOpen = file.open(QIODevice::Append);
    } else {
        bFileOpen = file.open(QIODevice::WriteOnly);
    }

    if (!bFileOpen) {
        return false;
    }

    QTextStream ts(&file);
    ts << content.toUtf8();
    file.close();

    return true;
}

//----------------------------------------------------------------------------
QMap<unsigned long, QString> QtdTools::getAllProcesses(QString contains)
{
    QMap<unsigned long, QString> ret;
#ifdef WIN32
    // Get the list of process identifiers.
    DWORD aProcesses[1024], cbNeeded, cProcesses;
    char szProcessName[MAX_PATH];
    unsigned int i;

    if ( !EnumProcesses( aProcesses, sizeof(aProcesses), &cbNeeded ) )
        return ret;

    // Calculate how many process identifiers were returned.
    cProcesses = cbNeeded / sizeof(DWORD);

    for ( i = 0; i < cProcesses; i++ ) {
        if( aProcesses[i] != 0 ) {
            HANDLE hProcess = OpenProcess( PROCESS_QUERY_INFORMATION |
                                           PROCESS_VM_READ,
                                           FALSE, aProcesses[i] );

            // Get the process name.

            if (NULL != hProcess )
            {
                HMODULE hMod;
                DWORD cbNeeded;

                if ( EnumProcessModules( hProcess, &hMod, sizeof(hMod), 
                     &cbNeeded) )
                {
                    GetModuleFileNameExA( hProcess, hMod, szProcessName, 
                                       sizeof(szProcessName)/sizeof(TCHAR) );
                    if (contains == "" || (QString(szProcessName).toLower().contains(contains.toLower()))) {
                        ret.insert(aProcesses[i], szProcessName);
                    }
                } else if (contains == "") {
                    ret.insert(aProcesses[i], "unknown");
                }

            } else if (contains == "") {
                ret.insert(aProcesses[i], "unknown");
            }
            CloseHandle( hProcess );            
        }
    }
#else
    QStringList dirs = QDir("e:/Projekte/VeriSens/devel/test").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QStringList processes = dirs.filter(QRegExp("^\\d+$"));
    foreach (QString proc, processes) {
        QString fileName = QFile(QString("/proc/%1/exe").arg(proc)).symLinkTarget();
        if (!fileName.isEmpty()) {
            fileName = QFileInfo(fileName).fileName();
            ret[proc.toULong()] = fileName;
        }
    }
#endif
    return ret;
}


//----------------------------------------------------------------------------
bool QtdTools::setProcessPriority(QProcess* pProc, ProcessPriority ePrio)
{
#ifdef WIN32
    if (!pProc || !pProc->pid()) {
        return false;
    }

    switch (ePrio) {
        case eProcessPriority_Idle:
            return SetPriorityClass(pProc->pid()->hProcess, IDLE_PRIORITY_CLASS) > 0;
            break;
        case eProcessPriority_Low:
            return SetPriorityClass(pProc->pid()->hProcess, BELOW_NORMAL_PRIORITY_CLASS) > 0;
            break;
        case eProcessPriority_High:
            return SetPriorityClass(pProc->pid()->hProcess, ABOVE_NORMAL_PRIORITY_CLASS) > 0;
            break;
        case eProcessPriority_Higher:
            return SetPriorityClass(pProc->pid()->hProcess, HIGH_PRIORITY_CLASS) > 0;
            break;
        case eProcessPriority_Realtime:
            return SetPriorityClass(pProc->pid()->hProcess, REALTIME_PRIORITY_CLASS) > 0;
            break;
        case eProcessPriority_Normal:
        default:
            return SetPriorityClass(pProc->pid()->hProcess, NORMAL_PRIORITY_CLASS) > 0;
            break;
    }
#endif
    return false;
}


