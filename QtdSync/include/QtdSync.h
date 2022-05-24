#ifndef QTDSYNC_H
#define QTDSYNC_H

#include "QtdBase.h"
#include "QtdApplication.h"
#include "QtdTools.h"
#include "QtdMail.h"

#include "ui_QtdSyncDlg.h"
#include "ui_QtdSyncConfigDlg.h"
#include "ui_QtdSyncVDirConfigDlg.h"
#include "ui_QtdSyncStartupDlg.h"
#include "ui_QtdSyncAboutDlg.h"
#include "ui_QtdSyncUpdateDlg.h"
#include "ui_QtdSyncSettingsDlg.h"
#include "ui_QtdSyncRemoteUserDlg.h"
#include "ui_QtdSyncServerDlg.h"
#include "ui_QtdSyncMailDlg.h"

#ifdef WIN32
#include <windows.h>
#endif

#define PEXEC(name) m_binPath + EXEC(name)

class QtdSync : public QtdApplication, public Ui::QtdSync
{
    Q_OBJECT
public:
    typedef enum {
         eNone
        ,eMain
        ,eStartup
        ,eAbout
        ,eTray
        ,eServer
        ,eServerStart
        ,eServerStop
    } ShowDialog;

    typedef enum {
         eDifferential
        ,eSynchronize
    } BackupType;

    typedef enum {
         eOnPlugin
        ,eOnStartup
        ,eOnShutdown
        ,eOnTime
    } ScheduleType;

    typedef enum {
         eStartOnly   = 0x01
        ,eStopOnly    = 0x02
        ,eStartOrStop = 0x03
    } EnServerRunMode;

    typedef struct {
        QString name;
        QString folder;
        QString exclude;
        QStringList filesExcluded;
        QString rsyncExpert;
        bool    bCreateSubFolder;
    } Folder;

    typedef struct {
        bool            bSilent;
        bool            bOnPlugin;
        bool            bOnShutdown;
        bool            bOnStartup;
        bool            bOnTime;
        bool            bMarkFailedAsValid;
        int             nConditionCount;
        QString         conditionType;

        bool            bRetry;
        int             nRetryNumber;
        QString         retryType;

        QDateTime       startTime;
        QDateTime       lastTry;
        int             nEveryCount;
        QString         everyType;
    } Schedule;

    typedef struct {
        bool            bRemote;
        QString         dest;
        QString         path;
        bool            bUserAuth;
        QString         user;
        QString         password;
        bool            bUseSSH;
        bool            bUsePassword;
        bool            bStorePassWd;
    } Destination;

    typedef struct {
        QString         fileName;
        QString         name;
        bool            bCreateSubfolder;
        Destination     destination;
        QList<Folder>   folders; 
        bool            isNew;
        bool            unSaved;
        BackupType      type;
        QDateTime       lastBackup;
        QDateTime       lastBackupTry;
        QString         uuid;
        Schedule        schedule;
        int             scheduledPrio;
        QPair<QString, QString> preProcessing;
        QPair<QString, QString> postProcessing;
        bool            sendReport;
        bool            sendReportOnErrorOnly;
        QtdMail         mail;
        bool            bCurrentlyBackingUp;
        QString         globalRsyncOptions;
    } Config;

    typedef struct {
        QString         name;
        QString         path;
        QString         description;
        bool            bReadOnly;
        QStringList     users;
    } VirtualDirConfig;

    typedef struct {
        bool            bSelf;
        bool            bActive;
        bool            bDriveLetters;
        QList<QtdTools::DriveType> driveTypes;
        unsigned long   nProcessId;
        unsigned long   nServerProcessId;
    } Monitor;

    typedef struct {
        bool            doUpdate;
        bool            showFolderBindingOptions;
        bool            showRsyncExpertSettings;
        bool            setProcessPrio;
        QtdTools::ProcessPriority processPrio;
        QString         proxyHost;
        int             proxyPort;
        QLocale::Language language;
        QString         proxyUser;
        QString         proxyPassWd;
        QMap<QString, QString> knownQtd;
        QString         updateLocation;
        Monitor         monitor;
        QMap<QString, Schedule> schedule;
        QMap<QString, QDateTime> lastModified;
        QString         computerUuid;
        bool            bSaveComputerConfig;
    } Settings;

    typedef struct {
        QString                 logFile;
        QMap<QString, QString>  users;
        QList<VirtualDirConfig> dirs;
        bool                    bSelf;
        QString                 path;
    } ServerSettings;

    QtdSync(int &argc, char** argv, ShowDialog showDlg, QStringList files = QStringList(), QtdApplication::QtdConfig config = QtdApplication::QtdConfig());
    ~QtdSync();

    bool            doBackup(QStringList files, QMap<QString, QString> config, QMap<QString, QVariant>* pStats = 0L);
    void            checkForRSync();
    void            commitData(QSessionManager& manager);
    void            runOrStopServer(EnServerRunMode mode = eStartOrStop);

public slots:
    void            slot_nameChanged(QString);
    void            slot_destEdited();
    void            slot_sshDestEdited(const QString& text);
    void            slot_pathSelected(const QString&);
    void            slot_nameSelected(int id);
    void            slot_subfolderStateChanged(int, bool bShowMessage = true);
    void            slot_typeChanged(int);
    void            slot_newFile();
    void            slot_newFileCopy();
    void            slot_openFile();
    void            slot_save();
    void            slot_saveAs();

    bool            slot_addFolder(QString folder = "");
    void            slot_addTask();
    void            slot_addPreScript(QString script = "");
    void            slot_addPostScript(QString script = "");
    void            slot_removeFolder();

    void            slot_editFolder();
    void            slot_excludeAdd();
    void            slot_excludeRemove();
    void            slot_browseFolderPath();
    void            slot_browseExecPath();
    void            slot_excludeEdited(const QString&);
    void            slot_configRsyncReset();
    void            slot_configSubfolderStateChanged(int, bool bShowMessage = true);
    void            slot_configSubfolderNameChanged(QString);

    void            slot_removeBackupSet();
    void            slot_browseDestination(QString dest = "");
    void            slot_remoteDestination();
    void            slot_remoteSSHDestination(bool bBrowse = true);
    void            slot_setDestination();

    void            slot_setRemoteUserAuth(bool bSetServer = false);
    void            slot_remoteSSHChecked(int state);
    void            slot_browseKeyFile();

    void            slot_checkModified();
    void            slot_checkForUpdate();
    void            slot_forcedCheckForUpdate();
    void            slot_about();
    void            slot_settings();
    void            slot_schedule();
    void            slot_mailTest();
    void            slot_mail();
    void            slot_accept();
    void            slot_cancel();
    void            slot_globalRsyncChanged(QString);
    void            slot_globalRsyncReset();
    void            slot_showStartup();
    void            slot_doBackup();
    void            slot_abortBackup();
    void            slot_doRestore();
    void            slot_elementSelected(QTreeWidgetItem*, int);
    void            slot_itemExpanded(QTreeWidgetItem*);
    void            slot_startupItemChanged(QTreeWidgetItem*, int);
    void            slot_startupItemDoubleClicked(QTreeWidgetItem*, int);
    void            slot_checkAll();
    void            slot_uncheckAll();
    void            slot_updateAvailable(const QStringList& description, const QString& version);
    void            slot_updateProgress(QString, int);
    void            slot_update();
    void            slot_updateFinished(bool, QStringList);

    void            slot_authenticationRequired(const QString &, quint16, QAuthenticator*);
    void            slot_proxyAuthenticationRequired(const QNetworkProxy&, QAuthenticator*);

    void            slot_bindToQtdSyncDrive();
    void            slot_bindToQtdFile();
    void            slot_bindToQtdDrive();
    void            slot_bindToComputer();
    void            slot_beforeRestart(bool&);

    void            slot_monitor();
    void            slot_monitorOpenQtdSync();
    void            slot_monitorOpenQtdSyncServer();
    void            slot_monitorCheckedScheduled();
    void            slot_monitorActivated(QSystemTrayIcon::ActivationReason);

    void            slot_monitorServer();
    void            slot_runServer();
    void            slot_addUser();
    void            slot_removeUser();
    void            slot_editUser();
    void            slot_browseVDirPath();
    void            slot_dataDropped(QStringList);
    void            slot_dataTextDropped(QString);

    void            slot_updateFileSizeEstimations();

protected:
    void            initDlg();
    void            showDlg(ShowDialog dlg);
    void            fillStartupDlg(QStringList files);
    void            saveConfig(bool bMonitorOnly = false);
    void            loadConfig(bool bClearQtd = false);
    void            fillTreeElement(QTreeWidgetItem*, bool, int);
    void            clearTreeElement(QTreeWidgetItem*);
    QString         browseForFolder(QString name = "", bool bIsFile = false, QString caption = "");
    QPair<QStringList, QStringList> validateKnownQtd();
    bool            addToKnownQtd(QString fileName);
    bool            doSchedulePlugin(QString filename, QString uuid, ScheduleType type, bool&);
    bool            askForBackup(QString name, ScheduleType type);
    void            localDestinationMissing();
    bool            isMonitorRunning();
    bool            isServerRunning();
    QStringList     remotePaths(Destination& destination);
    void            checkFstab();
    void            updateFreeSpaceText(QString dir);
    QString         lastBackupString(QDateTime dt);

    QMap<QString, QVariant> doDryRun();

    bool            sendBackupReport(Config& config, QString log, bool bFailed);


    bool            readQtd(QString content, QString filename = "");
    QString         getQtdUuid(QString filename);
    QString         getQtdName(QString filename);
    bool            setQtdUuid(QString filename, QString uuid);
    bool            openQtd(QString filename);
    QString         writeQtd(bool bRealSaving = false);
    void            clearQtd(bool bCheckName = true);
    bool            saveQtd();
    bool            setupConf(bool bDelete = false);

    void            getCurrentServerPid();
    void            setCurrentServerPid(unsigned long);
    void            updateServerStatus();
    void            updateServerDirectories(int nTopId = -1);
    void            updateServerUsers();

    bool            qtdHasChanged();

    bool            editFolder(Folder& fold);
    bool            editProcessingScript(bool bPre, QString& path, QString& args);
    bool            editVirtualDir(VirtualDirConfig& dir);
    bool            editUser(QString& user, QString& password);
    QString         evaluateString(QString str, bool bNice = false);
    QString         realDestinationFolder(QString folderName = "");

private:
    Config          m_config;
    QDialog*        m_pDlg;
    QDialog*        m_pTmpDlg;
    QDialog*        m_pUpdateDlg;

    QString         m_curQtd;
    QTimer*         m_timer;
    bool            m_bRebuildTree;
    int             m_nFillingTreeDepth;

    ShowDialog      m_currentDlg;
    QProcess*       m_pProcess;
    bool            m_bAborted;
    Settings        m_settings;
    ServerSettings  m_serverSettings;
    QTranslator     m_translator;
    QCompleter*     m_pRsyncCompleter;
    
    QStringList     m_updateInfo;
    QString         m_updateVersion;

    Ui::QtdSyncAbout       uiAbout;
    Ui::QtdSyncStartup     uiStartup;
    Ui::QtdSyncConfig      uiConfig;
    Ui::QtdSyncVDirConfig  uiVDirConfig;
    Ui::QtdSyncUpdate      uiUpdate;
    Ui::QtdSyncSettings    uiSettings;
    Ui::QtdSyncRemoteUser  uiRemoteUser;
    Ui::QtdSyncServer      uiServer;
    Ui::QtdSyncMail        uiMail;

    QSystemTrayIcon*       m_pTrayIcon;
    QMenu*                 m_pTrayMenu;
    QAction*               m_pTrayIsActive;
    QAction*               m_pAbortBackup;

    QString                m_tmpString;
    QString                m_tmpString2;
    QString                m_binPath;
    QString                m_homePath;

    QMap<QString, QString> p_config;

    bool            m_bSSHAvailable;
    bool            m_bDisableQtdChanged;
    QStringList     m_currentRemoved;
    QByteArray      m_geometry;
    

};

#endif // QTDSYNC_H