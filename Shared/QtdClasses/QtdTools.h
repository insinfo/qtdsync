#ifndef QTDTOOLS_H
#define QTDTOOLS_H

#include "QtdBase.h"

#define qtdMantisLink(id) QString("http://mantis.doering-thomas.de/set_project.php?project_id=" #id "&ref=bug_report_page.php")

#ifndef WIN32
#define Sleep(ms) sleep(ms / 1000)
#define IMPFAULT QMessageBox::information(0L, "Implementation Fault!", QString("%1, %2").arg(__FILE__).arg(__LINE__))
#define EXE_EXTENSION QString("")
#else
#define EXE_EXTENSION QString(".exe")
#endif

#define EXEC(name) name + EXE_EXTENSION
#define QtdDomElement(tagName, value) doc.createElement(tagName).appendChild(doc.createTextNode(value)).parentNode().toElement()


typedef struct _QtdFileEntryInfo {
    QString                 name;
    QString                 path;
    bool                    bIsDir;
    unsigned int            nRights;
    QDateTime               timeStamp;
    unsigned long long      nSize;
    QList<struct _QtdFileEntryInfo> files;
} QtdFileEntryInfo;

typedef QList<QtdFileEntryInfo> QtdFileEntryInfoList;

//----------------------------------------------------------------------------
class QtdTools
{
public:
    typedef struct {
        QString     name;
        QString     company;
        unsigned char version[4];
        QString     versionName;
        QString     copyright;
        QString     build;
    } ProductInfo;

    typedef enum {
         eDrive_Unknown     = 0x000
        ,eDrive_Floppy      = 0x001
        ,eDrive_Fixed       = 0x002
        ,eDrive_Removable   = 0x004
        ,eDrive_Network     = 0x008
        ,eDrive_CDROM       = 0x010
        ,eDrive_RamDisk     = 0x020
    } DriveType;

    typedef enum {
         eFreeSpace_kB
        ,eFreeSpace_MB
        ,eFreeSpace_GB
        ,eFreeSpace_TB
    } FreeSpaceType;

    typedef enum {
         eProcessPriority_Idle  = 0
        ,eProcessPriority_Low
        ,eProcessPriority_Normal
        ,eProcessPriority_High
        ,eProcessPriority_Higher
        ,eProcessPriority_Realtime
    } ProcessPriority;

    static bool                         removeDirectory(QString aDir);
    static bool                         removeDirectory(QDir aDir);
    static void                         productInfo(QString fileName, ProductInfo& info);
    static QString                      readFile(QString fileName);
    static bool                         writeFile(QString fileName, QString content, bool bAppend = false);
    static QMap<unsigned long, QString> getAllProcesses(QString contains);
    static DriveType                    getDriveType(char letter);
    static double                       freeSpace(QString dir, QtdTools::FreeSpaceType& type);
    static QStringList                  getCommandLineArguments(QString args);
    static bool                         setProcessPriority(QProcess* pProc, ProcessPriority ePrio);
    static QString                      codecSaveString(QString input);
};

#ifndef WIN32
int ShellExecuteA(int* pParent, char* cmd, char* file, char* args, char* dir, int nType);

#define ExitProcess(n) _exit(n)
#define SW_SHOW 0
#define SW_HIDE 0
#endif

#endif // QTDTOOLS_H
