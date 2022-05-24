#include "QtdSync.h"

#include "QtdBase.h"
#include "ui_QtdSyncAuthDlg.h"
#include "ui_QtdSyncFolderBindingDlg.h"
#include "ui_QtdSyncScheduledBackupDlg.h"
#include "ui_QtdSyncScheduleDlg.h"
#include "ui_QtdSyncSSHFolderDlg.h"

#include "QtdCrypt.h"

#ifdef WIN32
#include "windows.h"
#endif

#include "version.h"

#ifdef QTDSYNC_MESSAGES
QT_TRANSLATE_NOOP("QtdSpecialMessages", "UpdateMessage_v000.006.011")
#endif

QString lastError;

#define     QTD_VERSION             4
#define     QTD_CONFIG_VERSION      2
#define     QTD_ALL_RSYNC_OPTIONS   "--checksum|--hard-links|--whole-file|--one-file-system|--existing|--ignore-existing|--delete|--delete-after|--ignore-errors|--force|--partial|--ignore-times|--size-only|--skip-compress=*|--fuzzy|--bwlimit=120"
#define     QTD_DEF_RSYNC_OPTIONS   "--hard-links --delete --ignore-errors --force"

#ifndef WIN32
#define SetupUi(dlg) setupUi(dlg); dlg->setStyleSheet(qApp->styleSheet())
#else
#define SetupUi(dlg) setupUi(dlg)
#endif

//----------------------------------------------------------------------------
// init functions
//----------------------------------------------------------------------------
QtdSync::QtdSync(int& argc, char** argv, ShowDialog eDlg, QStringList files, QtdApplication::QtdConfig config)
: QtdApplication(argc, argv,config)
{
    // element initialization
    m_pDlg = 0L;
    m_pTmpDlg = 0L;
    m_pProcess = 0L;
    m_pUpdateDlg = 0L;
    m_timer = 0L;
    m_bRebuildTree = false;
    m_pTrayIcon = 0L;
    m_pTrayMenu = 0L;
    m_pAbortBackup = 0L;
    m_pTrayIsActive = 0L;
    m_bSSHAvailable = false;
    m_bDisableQtdChanged = false;
    m_nFillingTreeDepth = 0;

#ifdef Q_WS_X11
    m_binPath = "";
    qApp->setStyle("cleanlooks");
    qApp->setStyleSheet("QWidget { font-size: 11px; } QComboBox:lineEdit { font-size: 11px; } QLabel#m_pProduct { font-size: 13px;}  QLabel#m_pL_Caption { font-size: 13px;}");
#elif defined(Q_WS_WIN)
    m_binPath = qApp->applicationDirPath() + "/bin/";
#endif

    m_homePath = QDir::homePath();
#ifndef WIN32
    QString subHomePath(".qtdsync");

    if (!QDir(m_homePath).exists(subHomePath)) {
        if (!QDir(m_homePath).mkdir(subHomePath)) {
            m_homePath = QDir::homePath();
        } else {
            m_homePath += "/" + subHomePath;
        }   
    } else {
        m_homePath += "/" + subHomePath;
    }
    m_homePath.replace("\\", "/");
#endif

    m_serverSettings.bSelf = (eDlg >= eServer);

#ifdef WIN32
    checkForRSync();
#endif

    // check if we have writing access to the applicationPath
    m_appConfigFile = QDir::fromNativeSeparators(QDesktopServices::storageLocation(QDesktopServices::DataLocation)) + "/qtdsync.xml";
    QFile file(qApp->applicationDirPath() + "/dummyFile.tst");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QByteArray("dummyData"));
        file.close();
    }

    if (file.exists() && !QFile(m_appConfigFile).exists()) {
        // store file in local application path rather than in DataLocation
        m_appConfigFile = qApp->applicationDirPath() + "/qtdsync.xml";
    } else {
        // create data location path if not exist
        QFileInfo fi(m_appConfigFile);
        if (!fi.absoluteDir().exists()) {
            QDir(QDesktopServices::storageLocation(QDesktopServices::DataLocation)).mkpath(fi.absolutePath());
        }
    }

    loadConfig(true);

#ifdef QTD_CUSTOMIZED
# ifdef QTD_HAVE_UPDATECLIENT
    if (m_updateClient) {
        delete m_updateClient;
    }
# undef QTD_HAVE_UPDATECLIENT
# endif
#endif
#ifdef QTD_HAVE_UPDATECLIENT
    if (m_updateClient) {
        m_updateClient->setUpdateLocation(m_settings.updateLocation);
        m_updateClient->enableServerLogging(true);
        if (m_updateClient->updateFailed()) {
            QMessageBox::information(0L, tr("Update failed"), tr("<b>The just performed update failed.</b><br><br>You might want to download the full version from our <a href=\"http://qtdtools.doering-thomas.de/page.php?seite=QtdSync&sub=Download&lang=%1\">Download</a> page.").arg(tr("en")));
        }
    }
#endif

    QString rsync = PEXEC("rsync");
    rsync.replace("/", "\\");

    bool bUseStartup = false;

    // create completer for rsync params
    m_pRsyncCompleter = new QCompleter(this);
    m_pRsyncCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_pRsyncCompleter->setModel(new QStringListModel(QString(QTD_ALL_RSYNC_OPTIONS).split("|"), m_pRsyncCompleter));
    m_pRsyncCompleter->setModelSorting(QCompleter::CaseInsensitivelySortedModel);

#ifdef QTD_HAVE_UPDATECLIENT
    if (m_updateClient) {
        connect(m_updateClient, SIGNAL(beforeRestart(bool&)), this, SLOT(slot_beforeRestart(bool&)));
        connect(m_updateClient, SIGNAL(updateAvailable(const QStringList&, const QString&)),
            this, SLOT(slot_updateAvailable(const QStringList&, const QString&)));
        connect(m_updateClient, SIGNAL(authenticationRequired(const QString&, quint16, QAuthenticator*)),
            this, SLOT(slot_authenticationRequired(const QString&, quint16, QAuthenticator*)));
        connect(m_updateClient, SIGNAL(proxyAuthenticationRequired(const QNetworkProxy&, QAuthenticator*)),
            this, SLOT(slot_proxyAuthenticationRequired(const QNetworkProxy&, QAuthenticator*)));
        connect(m_updateClient, SIGNAL(updateSuccess(bool,QStringList)), this, SLOT(slot_updateFinished(bool,QStringList)));
        if (m_settings.proxyHost != "") {
            m_updateClient->setProxy(m_settings.proxyHost, m_settings.proxyPort);
        }
    }
#endif

    foreach (QString file, files) {
        if (!m_settings.knownQtd.keys().contains(QFileInfo(file).absoluteFilePath())) {
            addToKnownQtd(QFileInfo(file).absoluteFilePath());
        }
    }

    if (eDlg == eTray && isMonitorRunning()) {
        eDlg = eMain;
    }
            
    m_currentRemoved = validateKnownQtd().first;

    if (eDlg == eServerStart) {
        m_currentDlg = eDlg;
        runOrStopServer(eStartOnly);
    } else if (eDlg == eServerStop) {
        m_currentDlg = eDlg;
        runOrStopServer(eStopOnly);
    } else if (eDlg != eNone && eDlg != eTray && eDlg != eServer) {
        // check if there are any qtd-files in place
        QStringList curFiles = QDir(this->applicationDirPath()).entryList(QStringList("*.qtd"), QDir::Files, QDir::Name);
        foreach (QString curFile, curFiles) {
            if (!m_settings.knownQtd.keys().contains(QFileInfo(this->applicationDirPath() + "/" + curFile).absoluteFilePath())) {
                addToKnownQtd(QFileInfo(this->applicationDirPath() + "/" + curFile).absoluteFilePath());
            }
        }

        if (files.count() == 0 && m_settings.knownQtd.keys().count() > 0) {
            files << m_settings.knownQtd.keys();
            bUseStartup = true;
        }
        
        if (files.count() > 1 || bUseStartup) {
            showDlg(eStartup);
            fillStartupDlg(files);

            m_timer = new QTimer(this);
            connect(m_timer,                SIGNAL(timeout()),           this, SLOT(slot_checkModified()));
            m_timer->start(2000);
        } else {
            if (files.count() == 1) {
                QString fileName = files.at(0);
                openQtd(fileName);
            }
            showDlg(eMain);
            initDlg();

            m_timer = new QTimer(this);
            m_timer->start(500);

            saveQtd();
            connect(m_timer,                SIGNAL(timeout()),           this, SLOT(slot_checkModified()));
        }
    } else if (eDlg == eTray) {
        m_settings.monitor.bActive = true;
        m_productName += " Monitor";
        bool bServerIsRunning = isServerRunning();
        m_pTrayIcon = new QSystemTrayIcon(QIcon(bServerIsRunning ? ":/images/schedule_server.png" : ":/images/schedule.png"), this);
        m_pTrayMenu = new QMenu(m_productName, 0L);
        QAction* pAct = m_pTrayMenu->addAction(QIcon(":/images/newIcon.png"), tr("Open QtdSync..."), this, SLOT(slot_monitorOpenQtdSync()));
        QFont font = pAct->font();
        font.setBold(true);
        pAct->setFont(font);

#ifdef WIN32
        pAct = m_pTrayMenu->addAction(tr("Open QtdSync Server..."), this, SLOT(slot_monitorOpenQtdSyncServer()));
#endif
        m_pAbortBackup = m_pTrayMenu->addAction(tr("Abort current backup"), this, SLOT(slot_abortBackup()));
        m_pAbortBackup->setEnabled(false);

        m_pTrayMenu->addSeparator();
        m_pTrayIsActive = m_pTrayMenu->addAction(tr("Active"), this, SLOT(slot_monitorCheckedScheduled()));
        m_pTrayIsActive->setCheckable(true);
        m_pTrayIsActive->setChecked(m_settings.monitor.bActive);
        m_pTrayMenu->addSeparator();
        m_pTrayMenu->addAction(tr("Settings")+"...", this, SLOT(slot_settings()));
        m_pTrayMenu->addAction(QIcon(":/images/update.png"), tr("Check for update"), this, SLOT(slot_forcedCheckForUpdate()));
        m_pTrayMenu->addSeparator();
        m_pTrayMenu->addAction(QIcon(":/images/about.png"), tr("About QtdSync"), this, SLOT(slot_about()));
        m_pTrayMenu->addAction(QIcon(":/images/cancel.png"), tr("Exit"), this, SLOT(quit()));

        m_pTrayIcon->setContextMenu(m_pTrayMenu);
        m_pTrayIcon->show();
        QString toolTip = "QtdSync Monitor: " + (m_settings.monitor.bActive ? tr("Active"):tr("Not Active"));
#ifdef WIN32
        toolTip += "\nQtdSync Server: " + (bServerIsRunning ? tr("Running.") : tr("Stopped!"));
#endif
        m_pTrayIcon->setToolTip(toolTip);

#ifdef WIN32
        m_settings.monitor.nProcessId = GetCurrentProcessId();
#else
	    m_settings.monitor.nProcessId = qApp->applicationPid();
#endif
        m_settings.monitor.bSelf = true;
        saveConfig();
        m_currentDlg = eTray;

        // do startup backups
        if (QtdTools::getAllProcesses(QFileInfo(qApp->applicationFilePath()).fileName()).count() == 1) {
            m_currentRemoved << m_settings.knownQtd.keys();

        }

        m_timer = new QTimer(this);
        m_timer->start(5000);

        connect(m_timer,                SIGNAL(timeout()),           this, SLOT(slot_monitor()));
        connect(m_pTrayIcon,            SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(slot_monitorActivated(QSystemTrayIcon::ActivationReason)));

        disconnect(this,                SIGNAL(lastWindowClosed()));
    } else if (eDlg == eServer) {
        m_serverSettings.bSelf = true;
        m_productName += " Server";
        showDlg(eServer);
        initDlg();

        m_timer = new QTimer(this);
        m_timer->start(1000);

        connect(m_timer,                SIGNAL(timeout()),           this, SLOT(slot_monitorServer()));

    }
    saveConfig();
}

//----------------------------------------------------------------------------
QtdSync::~QtdSync()
{

    saveConfig();

    if (m_pDlg) {
        delete m_pDlg;
    }

    if (m_pTrayMenu) {
        delete m_pTrayMenu;
    }

    if (m_pTrayIcon) {
        m_pTrayIcon->hide();
        delete m_pTrayIcon;
    }
}

//----------------------------------------------------------------------------
void QtdSync::getCurrentServerPid()
{
    QString fileName = m_homePath + "/qtdsyncserver.xml";
    QDomDocument curDom;

    if (!curDom.setContent(QtdTools::readFile(fileName))) {
        m_settings.monitor.nServerProcessId = 0;
    } else {
        QDomNode root = curDom.firstChildElement("QtdSyncConfig");
        if (!root.isNull()) {
            QDomElement server = root.firstChildElement("server");
            unsigned long pid = server.attribute("process", "0").toULong();

            QMap<unsigned long, QString> procs = QtdTools::getAllProcesses(EXEC("rsync"));

            if (procs.contains(pid)) {
                m_settings.monitor.nServerProcessId = pid;
                QDomElement pNode = server.firstChildElement("path");
                if (!pNode.isNull()) {
                    m_serverSettings.path = pNode.text();
                }   
            } else {
                setCurrentServerPid(0);
                m_settings.monitor.nServerProcessId = 0;
            }
        }
    }
}

//----------------------------------------------------------------------------
void QtdSync::setCurrentServerPid(unsigned long pid)
{
    QDomDocument doc("QtdSyncConfig");
    doc.appendChild(doc.createProcessingInstruction( "xml",QString("version=\"1.0\" encoding=\"%1\"" ).arg("utf-8")));

    QDomElement root = doc.createElement("QtdSyncConfig");
    doc.appendChild(root);
    QDomElement nNode;
    QFile file;
    if (/* m_settings.monitor.bSelf || */ m_serverSettings.bSelf) {
        nNode = doc.createElement("server");
        nNode.setAttribute("process", QString("%1").arg(pid));

        QDomElement pNode = doc.createElement("path");
        pNode.appendChild(doc.createTextNode(m_serverSettings.path));
        nNode.appendChild(pNode);
        root.appendChild(nNode);

        file.setFileName(m_homePath + "/qtdsyncserver.xml");
        if (file.open(QIODevice::WriteOnly)) {
            QTextStream ts(&file);
            ts << doc.toString(4);
            file.close();
        }
    }
}


//----------------------------------------------------------------------------
void QtdSync::loadConfig(bool bClearQtd)
{
    QDomDocument curDom;
    if (bClearQtd) {
        m_curQtd = "";
        clearQtd();
    }

    m_settings.knownQtd.clear();
    QString fileName;

    int step = 0;
    m_settings.doUpdate = true;
    m_settings.proxyHost = "";
    m_settings.proxyPort = 0;
    m_settings.updateLocation = m_productUpdateLocation;
    m_settings.monitor.bActive = false;
    m_settings.monitor.nProcessId = 0;
    m_settings.monitor.bSelf = false;
    m_settings.showFolderBindingOptions = true;
    m_settings.showRsyncExpertSettings = false;
    m_settings.setProcessPrio = false;
    m_settings.processPrio = QtdTools::eProcessPriority_Normal;

    m_settings.bSaveComputerConfig = true;
    m_settings.language            = (QLocale::Language)0;

    m_settings.monitor.bActive      = false;
#ifdef WIN32
    m_settings.monitor.bDriveLetters= true;
#else
    m_settings.monitor.bDriveLetters= false;
#endif
    m_settings.monitor.driveTypes << QtdTools::eDrive_Fixed << QtdTools::eDrive_Removable << QtdTools::eDrive_Network;
    
    QString serverPath = m_binPath;

    getCurrentServerPid();
    
    if (isServerRunning()) {
        serverPath = m_serverSettings.path;
    }

    bool reSave = false;

    while (step < 4) {
        if (step == 0) {
            fileName = m_homePath + "/qtdsync.xml";
        } else if (step == 1) {
            fileName = qApp->applicationDirPath() + "/qtdsync.xml";
        } else if (step == 2) {
            fileName = m_homePath + "/qtdsyncmonitor.xml";
        } else {
            fileName = QFileInfo(serverPath + "/../qtdsyncserver.xml").absoluteFilePath();
        }

        if (!curDom.setContent(QtdTools::readFile(fileName))) {
            step++;
            continue;
        }

        QDomNode root = curDom.firstChildElement("QtdSyncConfig");
        if (root.isNull()) {
            step++;
            continue;
        }

        if (step == 0) {
            if (root.toElement().hasAttribute("version")) {
                m_settings.bSaveComputerConfig = root.toElement().attribute("version").toInt() <= QTD_CONFIG_VERSION;
            }
            m_settings.computerUuid = "";
            if (root.toElement().hasAttribute("computerUuid")) {
                m_settings.computerUuid = root.toElement().attribute("computerUuid");
            } 
            if (m_settings.computerUuid == "") {
                m_settings.computerUuid = QUuid::createUuid().toString();
                reSave = true;
            }
        } else if (step == 1) {
            if (root.toElement().hasAttribute("folderBindOptions")) {
                m_settings.showFolderBindingOptions = root.toElement().attribute("folderBindOptions") == "true";
            }
            if (root.toElement().hasAttribute("rsyncExpertSettings")) {
                m_settings.showRsyncExpertSettings = root.toElement().attribute("rsyncExpertSettings") == "true";
            }

#ifdef WIN32
            if (root.toElement().hasAttribute("backupProcessPriority")) {
                m_settings.setProcessPrio = true;
                m_settings.processPrio = (QtdTools::ProcessPriority)root.toElement().attribute("backupProcessPriority").toInt();
            }
#endif
            if (root.toElement().hasAttribute("language")) {
                m_settings.language = (QLocale::Language)root.toElement().attribute("language").toInt();
                loadTranslations(m_settings.language);
            }

            QDomElement nNode = root.firstChildElement("geometry");
            if (!nNode.isNull()) {
                m_geometry = QByteArray::fromBase64(nNode.text().toAscii());
            }
        }

        if (step < 2) {
            QDomElement knownQtd = root.firstChildElement("knownQtd");
            QDomNodeList nLst = knownQtd.elementsByTagName("qtd");
            for (int i = 0; i < nLst.count(); i++) {
                QString curName = nLst.at(i).toElement().text();
                if (!m_settings.knownQtd.contains(QFileInfo(curName).absoluteFilePath())) {
                    if (step == 0 || QFileInfo(curName).exists()) {
                        if (!addToKnownQtd(QFileInfo(curName).absoluteFilePath()) && step == 0 && !m_settings.knownQtd.values().contains(nLst.at(i).toElement().attribute("uuid"))) {
                            m_settings.knownQtd.insert(QFileInfo(curName).absoluteFilePath(), nLst.at(i).toElement().attribute("uuid"));
                        }
                    }
                }
            }

            knownQtd = root.firstChildElement("update");
            if (!knownQtd.isNull()) {
                if (step == 1) m_settings.doUpdate = knownQtd.attribute("doIt", "true") == "true" ? true : false;
                if (m_settings.proxyHost == "") {
                    if (knownQtd.hasAttribute("proxyHost")) {
                        m_settings.proxyHost = knownQtd.attribute("proxyHost", "");
                    }
                    if (knownQtd.hasAttribute("proxyPort")) {
                        m_settings.proxyPort = knownQtd.attribute("proxyPort", "0").toInt();
                    }
                }
                if (knownQtd.text() != "") {
                    m_settings.updateLocation = knownQtd.text();
                }
            }
        }

        if (step == 0 || step == 2) {
            QDomElement knownQtd = root.firstChildElement("monitor");
#ifdef WIN32
            if (step == 0) m_settings.monitor.bDriveLetters = knownQtd.attribute("driveLetters", "true") == "true" ? true:false;
#else
            m_settings.monitor.bDriveLetters = false;
#endif
            if (step == 0) {
                m_settings.monitor.driveTypes.clear();
                QStringList driveTypes = knownQtd.attribute("driveTypes", "Fixed|Removable|Network").split("|");
                foreach (QString driveType, driveTypes) {
                    if (driveType == "Fixed") {
                        m_settings.monitor.driveTypes << QtdTools::eDrive_Fixed;
                    } else if (driveType == "Removable") {
                        m_settings.monitor.driveTypes << QtdTools::eDrive_Removable;
                    } else if (driveType == "CD/DVD") {
                        m_settings.monitor.driveTypes << QtdTools::eDrive_CDROM;
                    } else if (driveType == "Network") {
                        m_settings.monitor.driveTypes << QtdTools::eDrive_Network;
                    } else if (driveType == "RamDisk") {
                        m_settings.monitor.driveTypes << QtdTools::eDrive_RamDisk;
                    } else if (driveType == "Floppy") {
                        m_settings.monitor.driveTypes << QtdTools::eDrive_Floppy;
                    }
                }
            }
            if (step == 2) {
                m_settings.monitor.nProcessId    = knownQtd.attribute("process", "0").toULong();
            }
        }

        if (step == 3) {
            m_serverSettings.logFile = "";
            m_serverSettings.dirs.clear();
            QDomElement sRoot = root.firstChildElement("server");
            if (sRoot.hasAttribute("logFile")) {
                m_serverSettings.logFile = sRoot.attribute("logFile","");
            }

            QDomNodeList nLst = sRoot.elementsByTagName("user");
            for (int i = 0; i < nLst.count(); i++) {
                if (nLst.at(i).toElement().text() == "") {
                    continue;
                }

                QString user = nLst.at(i).toElement().text();
                QString password = "";
                
                if (nLst.at(i).toElement().hasAttribute("password")) {
                    password = nLst.at(i).toElement().attribute("password");
                }

                m_serverSettings.users[user] = password;
            }

            nLst = sRoot.elementsByTagName("dir");
            QStringList dirs;
            for (int i = 0; i < nLst.count(); i++) {
                VirtualDirConfig dir;
                if (!nLst.at(i).toElement().hasAttribute("name") || nLst.at(i).toElement().attribute("name", "") == "") {
                    continue;
                }

                QDomElement sNode = nLst.at(i).firstChildElement("path");
                if (!sNode.isNull()) {
                    dir.path        = sNode.text();
                }
                dir.bReadOnly   = nLst.at(i).toElement().attribute("readOnly", "false") == "true";
                dir.name        = nLst.at(i).toElement().attribute("name", "");

                if (dirs.contains(dir.name)) {
                    continue;
                } else {
                    dirs << dir.name;
                }

                sNode = nLst.at(i).firstChildElement("description");
                if (!sNode.isNull()) {
                   dir.description =  sNode.text();
                }   

                sNode = nLst.at(i).firstChildElement("users");
                if (!sNode.isNull()) {
                    dir.users << sNode.text().split(",");
                }

                m_serverSettings.dirs << dir;
            }
        }

        step++;
    }

    if (reSave) {
        saveConfig();
    }
}

//----------------------------------------------------------------------------
void QtdSync::saveConfig(bool bMonitorOnly)
{
    QDomDocument doc("QtdSyncConfig");
    doc.appendChild(doc.createProcessingInstruction( "xml",QString("version=\"1.0\" encoding=\"%1\"" ).arg("utf-8")));

    QDomElement root = doc.createElement("QtdSyncConfig");
    doc.appendChild(root);
    QDomElement nNode;
    QFile file;
    if (m_settings.monitor.bSelf) {
        nNode = doc.createElement("monitor");
        nNode.setAttribute("process", QString("%1").arg(m_settings.monitor.nProcessId));
        root.appendChild(nNode);

        file.setFileName(m_homePath + "/qtdsyncmonitor.xml");
        if (file.open(QIODevice::WriteOnly)) {
            QTextStream ts(&file);
            ts << doc.toString(4).toUtf8();
            file.close();
        }

        root.removeChild(nNode);
        if (bMonitorOnly) {
            return;
        }
    }

    QString serverFile = qApp->applicationDirPath() + "/qtdsyncserver.xml";
    if (isServerRunning()) {
        serverFile = QFileInfo(m_serverSettings.path + "/../qtdsyncserver.xml").absoluteFilePath();
    }

    if (m_serverSettings.bSelf) {
        nNode = doc.createElement("server");
        if (m_serverSettings.logFile != "") {
            nNode.setAttribute("logFile", m_serverSettings.logFile);
        }
        root.appendChild(nNode);

        QDomElement vNode = doc.createElement("users");
        foreach (QString user, m_serverSettings.users.keys()) {
            QDomElement uNode = doc.createElement("user");
            uNode.appendChild(doc.createTextNode(user));
            if (m_serverSettings.users[user] != "") {
                uNode.setAttribute("password", m_serverSettings.users[user]);
            }
            vNode.appendChild(uNode);
        }
        nNode.appendChild(vNode);


        vNode = doc.createElement("virtualDirectories");
        for (int i = 0; i < m_serverSettings.dirs.count(); i++) {
            VirtualDirConfig dir = m_serverSettings.dirs.at(i);
            
            if (dir.name == "" || dir.path == "") {
                continue;
            }

            QDomElement dNode = doc.createElement("dir");
            dNode.setAttribute("name", dir.name);
            if (dir.bReadOnly) dNode.setAttribute("readOnly", "true");

            if (dir.path != "") {
                QDomElement cNode = doc.createElement("path");
                cNode.appendChild(doc.createTextNode(dir.path));
                dNode.appendChild(cNode);
            }

            if (dir.description != "") {
                QDomElement cNode = doc.createElement("description");
                cNode.appendChild(doc.createTextNode(dir.description));
                dNode.appendChild(cNode);
            }

            if (dir.users.count() > 0) {
                QDomElement cNode = doc.createElement("users");
                cNode.appendChild(doc.createTextNode(dir.users.join(",")));
                dNode.appendChild(cNode);
            }
            vNode.appendChild(dNode);
        }
        nNode.appendChild(vNode);

        file.setFileName(serverFile);
        if (file.open(QIODevice::WriteOnly)) {
            QTextStream ts(&file);
            ts << doc.toString(4).toUtf8();
            file.close();
        }

        root.removeChild(nNode);
    }


    nNode = doc.createElement("knownQtd");
    root.setAttribute("version", QTD_CONFIG_VERSION);

    foreach (QString qtd, m_settings.knownQtd.keys()) {
        QDomElement qNode = doc.createElement("qtd");
        qNode.setAttribute("uuid", m_settings.knownQtd[qtd]);
        qNode.appendChild(doc.createTextNode(qtd));
        nNode.appendChild(qNode);
    }
    root.appendChild(nNode);

    nNode = doc.createElement("geometry");
    nNode.appendChild(doc.createTextNode(m_geometry.toBase64()));
    root.appendChild(nNode);

    nNode = doc.createElement("update");
    nNode.setAttribute("doIt", m_settings.doUpdate ? "true" : "false");
    root.appendChild(nNode);

    if (m_settings.showFolderBindingOptions) {
        root.setAttribute("folderBindOptions", m_settings.showFolderBindingOptions ? "true" : "false");
    }

    if (m_settings.showRsyncExpertSettings) {
        root.setAttribute("rsyncExpertSettings", m_settings.showRsyncExpertSettings ? "true" : "false");
    }

#ifdef WIN32
    if (m_settings.setProcessPrio) {
        root.setAttribute("backupProcessPriority", (int)m_settings.processPrio);
    }
#endif
    if (m_settings.language != (QLocale::Language)0) {
        root.setAttribute("language", QString("%1").arg(m_settings.language));
    }


    file.setFileName(qApp->applicationDirPath() + "/qtdsync.xml");
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream ts(&file);
        ts << doc.toString(4).toUtf8();
        file.close();
    }

    if (!m_settings.bSaveComputerConfig) return;


    root.setAttribute("computerUuid", m_settings.computerUuid);
    root.removeAttribute("folderBindOptions");
    root.removeAttribute("rsyncExpertSettings");
    root.removeChild(root.firstChildElement("geometry"));

    nNode.removeAttribute("doIt");

    if (m_settings.proxyHost != "") {
        nNode.setAttribute("proxyHost", m_settings.proxyHost);
        nNode.setAttribute("proxyPort", QString("%1").arg(m_settings.proxyPort));
    }
    if (m_settings.updateLocation != "" && m_settings.updateLocation != m_productUpdateLocation) {
        nNode.appendChild(doc.createTextNode(m_settings.updateLocation));
    }

    nNode = doc.createElement("monitor");
    nNode.setAttribute("driveLetters", m_settings.monitor.bDriveLetters ? "true":"false");

    QStringList driveTypes;
    for (int i = 0; i < m_settings.monitor.driveTypes.count(); i++) {
        if (m_settings.monitor.driveTypes.at(i) == QtdTools::eDrive_Fixed) {
            driveTypes << "Fixed";
        } else if (m_settings.monitor.driveTypes.at(i) == QtdTools::eDrive_Removable) {
            driveTypes << "Removable";
        } else if (m_settings.monitor.driveTypes.at(i) == QtdTools::eDrive_CDROM) {
            driveTypes << "CD/DVD";
        } else if (m_settings.monitor.driveTypes.at(i) == QtdTools::eDrive_Network) {
            driveTypes << "Network";
        } else if (m_settings.monitor.driveTypes.at(i) == QtdTools::eDrive_RamDisk) {
            driveTypes << "RamDisk";
        } else if (m_settings.monitor.driveTypes.at(i) == QtdTools::eDrive_Floppy) {
            driveTypes << "Floppy";
        }
    }
    nNode.setAttribute("driveTypes", driveTypes.join("|"));
    root.appendChild(nNode);

    file.setFileName(m_homePath + "/qtdsync.xml");
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream ts(&file);
        ts << doc.toString(4).toUtf8();
        file.close();
    }

    if (m_serverSettings.bSelf) setupConf(!isServerRunning());

}

//----------------------------------------------------------------------------
void QtdSync::slot_checkAll()
{
    for (int i = 0; i < uiStartup.m_pTW_Sets->topLevelItemCount(); i++) {
        QTreeWidgetItem* pItem = uiStartup.m_pTW_Sets->topLevelItem(i);
        if (pItem) {
            pItem->setCheckState(0, Qt::Checked);
        }
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_uncheckAll()
{
    for (int i = 0; i < uiStartup.m_pTW_Sets->topLevelItemCount(); i++) {
        QTreeWidgetItem* pItem = uiStartup.m_pTW_Sets->topLevelItem(i);
        if (pItem) {
            pItem->setCheckState(0, Qt::Unchecked);
        }
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_startupItemDoubleClicked(QTreeWidgetItem*, int)
{
    slot_editFolder();
}

//----------------------------------------------------------------------------
void QtdSync::slot_startupItemChanged(QTreeWidgetItem* pThisItem, int)
{
    for (int i = 0; i < uiStartup.m_pTW_Sets->topLevelItemCount(); i++) {
        QTreeWidgetItem* pItem = uiStartup.m_pTW_Sets->topLevelItem(i);
        if (pItem->isSelected() && (pItem->flags() & Qt::ItemIsUserCheckable)) {
            pItem->setCheckState(0, pThisItem->checkState(0));
        }
    }
}

//----------------------------------------------------------------------------
void QtdSync::showDlg(ShowDialog dlg)
{
    ShowDialog curDlg = m_currentDlg;
    m_currentDlg = dlg;

    if (dlg == eMain) {
        m_pDlg = new QDialog(0L, Qt::CustomizeWindowHint | Qt::WindowTitleHint);
        SetupUi(m_pDlg);
        m_pUpdate->hide();

        m_pGB_BackupInfo_Local->hide();

        m_pLE_RsyncOptions->setCompleter(m_pRsyncCompleter);

        connect(m_pCB_Name,             SIGNAL(activated(int)),      this, SLOT(slot_nameSelected(int)));
        connect(m_pCB_Name,             SIGNAL(editTextChanged(QString)), this, SLOT(slot_nameChanged(QString)));
        connect(m_pChB_SubFolder,       SIGNAL(stateChanged(int)),      this, SLOT(slot_subfolderStateChanged(int)));
        connect(m_pCB_BackupType,       SIGNAL(currentIndexChanged(int)), this, SLOT(slot_typeChanged(int)));
        connect(m_pPB_Config_Add,       SIGNAL(clicked()),           this, SLOT(slot_addTask()));
        connect(m_pPB_Config_Remove,    SIGNAL(clicked()),           this, SLOT(slot_removeFolder()));
        connect(m_pPB_Config_Edit,      SIGNAL(clicked()),           this, SLOT(slot_editFolder()));
        connect(m_pPB_OK,               SIGNAL(clicked()),           this, SLOT(slot_accept()));
        connect(m_pPB_Cancel,           SIGNAL(clicked()),           this, SLOT(slot_cancel()));
        connect(m_pLE_RsyncOptions,     SIGNAL(textChanged(QString)), this, SLOT(slot_globalRsyncChanged(QString)));
        connect(m_pPB_RsyncReset,       SIGNAL(clicked()),           this, SLOT(slot_globalRsyncReset()));

        connect(m_pPB_About,            SIGNAL(clicked()),           this, SLOT(slot_about()));
        connect(m_pPB_Settings,         SIGNAL(clicked()),           this, SLOT(slot_settings()));
        connect(m_pPB_DoBackup,         SIGNAL(clicked()),           this, SLOT(slot_doBackup()));
        connect(m_pPB_DoRestore,        SIGNAL(clicked()),           this, SLOT(slot_doRestore()));
        connect(m_pTW_Folders,          SIGNAL(itemExpanded(QTreeWidgetItem*)), this, SLOT(slot_itemExpanded(QTreeWidgetItem*)));
        connect(m_pTW_Folders,          SIGNAL(itemClicked(QTreeWidgetItem*,int)), this, SLOT(slot_elementSelected(QTreeWidgetItem*,int)));
        connect(m_pUpdate,              SIGNAL(clicked()),           this, SLOT(slot_update()));
        connect(m_pPB_Dest_Browse,      SIGNAL(clicked()),           this, SLOT(slot_setDestination()));
        connect(m_pPB_Config_Schedule,  SIGNAL(clicked()),           this, SLOT(slot_schedule()));
        connect(m_pPB_Config_Mail,      SIGNAL(clicked()),           this, SLOT(slot_mail()));
        connect(m_pLE_Destination,      SIGNAL(returnPressed()),     this, SLOT(slot_destEdited()));
        connect(m_pCB_RemotePath,       SIGNAL(activated(QString)), this, SLOT(slot_pathSelected(QString)));
        connect(m_pPB_RemoteSubmit,     SIGNAL(clicked()),           this, SLOT(slot_destEdited()));
        connect(m_pPB_RemoteUser,       SIGNAL(clicked()),           this, SLOT(slot_setRemoteUserAuth()));
        connect(m_pPB_RefreshSizes,     SIGNAL(clicked()),           this, SLOT(slot_updateFileSizeEstimations()));
        connect(m_pTW_Folders,          SIGNAL(dataTextDropped(QString)), this, SLOT(slot_dataTextDropped(QString)));
        connect(m_pLE_Destination,      SIGNAL(dataTextDropped(QString)), this, SLOT(slot_dataTextDropped(QString)));
        connect(m_pTW_Folders,          SIGNAL(dataUrlsDropped(QStringList)), this, SLOT(slot_dataDropped(QStringList)));
        connect(m_pLE_Destination,      SIGNAL(dataUrlsDropped(QStringList)), this, SLOT(slot_dataDropped(QStringList)));
        connect(m_pLE_RemotePath,       SIGNAL(textEdited(QString)), this, SLOT(slot_sshDestEdited(QString)));
        connect(m_pCB_RemotePath,       SIGNAL(editTextChanged(QString)), this, SLOT(slot_sshDestEdited(QString)));


        // init file menu
        QMenu* pMenu = new QMenu(m_pDlg);
        pMenu->addAction(QPixmap(QString::fromUtf8(":/images/filenew.png")), tr("New"), this, SLOT(slot_newFile()));
        pMenu->addAction(tr("New, as copy of..."), this, SLOT(slot_newFileCopy()));
        pMenu->addAction(QPixmap(QString::fromUtf8(":/images/fileopen.png")), tr("Open"), this, SLOT(slot_openFile()));
        pMenu->addSeparator();
        pMenu->addAction(QPixmap(QString::fromUtf8(":/images/filesave.png")), tr("Save"), this, SLOT(slot_save()));
        pMenu->addAction(tr("Save as..."), this, SLOT(slot_saveAs()));
        m_pPB_File_New->setMenu(pMenu);
        m_pPB_File_New->setPopupMode(QToolButton::InstantPopup);

        m_pL_ProductInfo->setText(QString("<b>%1 v%2</b><br/><small>%3</small>").arg(m_productName).arg(m_productVersion).arg(m_productCopyright));

#ifndef Q_WS_MAC
        tabWidget->setCornerWidget(m_pW_LastBackup);
#endif
        m_pDlg->show();

    } if (dlg == eServer) {
        m_pDlg = new QDialog(0L, Qt::CustomizeWindowHint | Qt::WindowTitleHint);
        uiServer.SetupUi(m_pDlg);
        uiServer.m_pUpdate->hide();

        connect(uiServer.m_pPB_Config_Add,       SIGNAL(clicked()),           this, SLOT(slot_addFolder()));
        connect(uiServer.m_pPB_Config_Remove,    SIGNAL(clicked()),           this, SLOT(slot_removeFolder()));
        connect(uiServer.m_pPB_Config_Edit,      SIGNAL(clicked()),           this, SLOT(slot_editFolder()));
        connect(uiServer.m_pPB_OK,               SIGNAL(clicked()),           this, SLOT(slot_accept()));
        connect(uiServer.m_pPB_About,            SIGNAL(clicked()),           this, SLOT(slot_about()));
        connect(uiServer.m_pPB_RunServer,        SIGNAL(clicked()),           this, SLOT(slot_runServer()));
        connect(uiServer.m_pTW_Folders,          SIGNAL(itemExpanded(QTreeWidgetItem*)), this, SLOT(slot_itemExpanded(QTreeWidgetItem*)));
        //connect(uiServer.m_pTW_Folders,          SIGNAL(itemClicked(QTreeWidgetItem*,int)), this, SLOT(slot_elementSelected(QTreeWidgetItem*,int)));
        connect(uiServer.m_pUpdate,              SIGNAL(clicked()),           this, SLOT(slot_update()));
        connect(uiServer.m_pPB_Settings,         SIGNAL(clicked()),           this, SLOT(slot_settings()));

        connect(uiServer.m_pPB_User_Add,         SIGNAL(clicked()),           this, SLOT(slot_addUser()));
        connect(uiServer.m_pPB_User_Remove,      SIGNAL(clicked()),           this, SLOT(slot_removeUser()));
        connect(uiServer.m_pPB_User_Edit,        SIGNAL(clicked()),           this, SLOT(slot_editUser()));
        connect(uiServer.m_pTW_Folders,          SIGNAL(dataTextDropped(QString)), this, SLOT(slot_dataTextDropped(QString)));
        connect(uiServer.m_pTW_Folders,          SIGNAL(dataUrlsDropped(QStringList)), this, SLOT(slot_dataDropped(QStringList)));

        uiServer.m_pProduct->setText(m_productName + " v" + m_productVersion);
        uiServer.m_pCopyright->setText(m_productCopyright);

        if (m_productVersion.contains("beta") || m_productVersion.contains("pre-release")) {
            QString version = QString("%1.%2.%3").arg(m_productInfo.version[0]).arg(m_productInfo.version[1]).arg(m_productInfo.version[2]);
            uiServer.m_pL_BugReport->setText(QString("<a href=\"%1?product_version=%2\">" + tr("Report a bug!") + "</a>").arg(qtdMantisLink(1)).arg(version));
        } else {
            uiServer.m_pL_BugReport->hide();
        }
        m_pDlg->show();

    } else if (dlg == eStartup) {
        m_pDlg = new QDialog();
        uiStartup.SetupUi(m_pDlg);
        uiStartup.m_pProduct->setText(m_productName + " v" + m_productVersion);
        uiStartup.m_pCopyright->setText(m_productCopyright);

        connect(uiStartup.m_pPB_DoBackup, SIGNAL(clicked()),        this, SLOT(slot_doBackup()));
        connect(uiStartup.m_pPB_CreateBackup, SIGNAL(clicked()),    this, SLOT(slot_newFile()));
        connect(uiStartup.m_pPB_EditBackup, SIGNAL(clicked()),      this, SLOT(slot_editFolder()));
        connect(uiStartup.m_pTW_Sets, SIGNAL(itemClicked(QTreeWidgetItem*, int)), this, SLOT(slot_elementSelected(QTreeWidgetItem*, int)));
        connect(uiStartup.m_pPB_RemoveBackup, SIGNAL(clicked()),    this, SLOT(slot_removeBackupSet()));
        connect(uiStartup.m_pTW_Sets, SIGNAL(itemChanged(QTreeWidgetItem*, int)), this, SLOT(slot_startupItemChanged(QTreeWidgetItem*, int)));
        connect(uiStartup.m_pTW_Sets, SIGNAL(itemDoubleClicked(QTreeWidgetItem*, int)), this, SLOT(slot_startupItemDoubleClicked(QTreeWidgetItem*, int)));
        connect(uiStartup.m_pPB_CheckAll, SIGNAL(clicked()),        this, SLOT(slot_checkAll()));
        connect(uiStartup.m_pPB_UncheckAll, SIGNAL(clicked()),        this, SLOT(slot_uncheckAll()));

        m_pDlg->show();
    } else if (dlg == eAbout) {
        QDialog* pDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);

        uiAbout.SetupUi(pDlg);
        uiAbout.m_pProduct->setText(m_productName + " v" + m_productVersion);
        QString copyRight = m_productCopyright;
        uiAbout.textBrowser->setOpenExternalLinks(true);

        copyRight.replace("Thomas Döring", "<a href=\"http://www.doering-thomas.de?QtdSync\">Thomas Döring</a>");
        uiAbout.m_pCopyright->setText(copyRight);

        QString problems = uiAbout.m_pL_Problems->text();
        QString version = QString("%1.%2.%3").arg(m_productInfo.version[0]).arg(m_productInfo.version[1]).arg(m_productInfo.version[2]);
        problems.replace("report a bug", QString("<a href=\"%1?product_version=%2\">report a bug</a>").arg(qtdMantisLink(1)).arg(version));
        uiAbout.m_pL_Problems->setText(problems);
        pDlg->exec();

        delete pDlg;
        m_currentDlg = curDlg;
    }
}


//----------------------------------------------------------------------------
void QtdSync::fillStartupDlg(QStringList files)
{
    // reset GUI
    uiStartup.m_pTW_Sets->clear();
    uiStartup.m_pPB_EditBackup->setEnabled(false);
    uiStartup.m_pPB_RemoveBackup->setEnabled(false);

    QStringList curFiles;
    int count = 0;
    foreach(QString file, files) {
        QString fileName = file;
        bool bFileFound = openQtd(fileName);
        if (bFileFound && !m_settings.knownQtd.keys().contains(fileName)) {
            addToKnownQtd(fileName);
        }
        if (bFileFound) {
            count++;
            QTreeWidgetItem* pItem = new QTreeWidgetItem(uiStartup.m_pTW_Sets, QStringList(m_config.name));
            pItem->setToolTip(0, fileName);
            pItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            pItem->setCheckState(0, Qt::Checked);
            pItem->setData(0, Qt::UserRole, fileName);
            pItem->setIcon(0, QIcon(m_config.destination.bRemote ? (m_config.destination.bUseSSH ? ":/images/icon_green.png":":/images/icon_yellow.png") : ":/images/icon_blue.png"));

            QTreeWidgetItem* last = new QTreeWidgetItem(pItem, QStringList(tr("Last Backup") + ": " + lastBackupString(m_config.lastBackup)));
            QFont font = last->font(0);
            font.setBold(true);
            last->setFont(0, font);

            for (int i = 0; i < m_config.folders.count(); i++) {
                QString evalFolder = evaluateString(m_config.folders.at(i).folder);
                last = new QTreeWidgetItem(pItem, QStringList(evaluateString(m_config.folders.at(i).folder, true)));
                last->setIcon(0, QIcon(QDir(evalFolder).exists() ? ":/images/ok.png" : ":/images/delete.png"));
            }
        } else {
            curFiles << fileName;
        }
    }

    foreach(QString fileName, curFiles) {
        QTreeWidgetItem* pItem = new QTreeWidgetItem(uiStartup.m_pTW_Sets, QStringList(fileName));
        pItem->setToolTip(0, tr("File not found!"));
        pItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        pItem->setData(0, Qt::UserRole, fileName);
        pItem->setIcon(0, QIcon(":/images/delete.png"));
    }

    uiStartup.m_pPB_DoBackup->setEnabled(count > 0);
    uiStartup.m_pTW_Sets->expandAll();
}




//----------------------------------------------------------------------------
void QtdSync::checkForRSync()
{
    bool bMustMoveFiles = false;
    // since v0.5.1 the cygwin binaries must be located in the subfolder "bin"
    if (!QDir(qApp->applicationDirPath()).exists("bin")) {
        m_binPath = qApp->applicationDirPath() + "/";
        bMustMoveFiles = true;
    }

    if (!QFile(m_binPath + "rsync.exe").exists() ||
        !QFile(m_binPath + "cygiconv-2.dll").exists() ||
        !QFile(m_binPath + "cyggcc_s-1.dll").exists() ||
        !QFile(m_binPath + "cygwin1.dll").exists()) {

            QMessageBox::warning(0L, tr("rsync not found"), tr("rsync was not found in the program path. Doing backups will <b>not</b> work!")); 
            m_bSSHAvailable = false;
    } else {
        m_bSSHAvailable =   QFile(m_binPath + "cygcrypto-0.9.8.dll").exists() &&
                            QFile(m_binPath + "cyggcc_s-1.dll").exists() &&
                            QFile(m_binPath + "cygssp-0.dll").exists() &&
                            QFile(m_binPath + "cygz.dll").exists() && 
                            QFile(m_binPath + "ssh.exe").exists();

        if (bMustMoveFiles) {
            bMustMoveFiles &= QDir(m_binPath).mkdir("bin");
            bMustMoveFiles &= QFile::rename(m_binPath + "rsync.exe", m_binPath + "bin/rsync.exe");
            bMustMoveFiles &= QFile::rename(m_binPath + "cygiconv-2.dll", m_binPath + "bin/cygiconv-2.dll");
            bMustMoveFiles &= QFile::rename(m_binPath + "cyggcc_s-1.dll", m_binPath + "bin/cygiconv-2.dll");
            bMustMoveFiles &= QFile::rename(m_binPath + "cygwin1.dll", m_binPath + "bin/cygwin1.dll");

            if (m_bSSHAvailable) {
                bMustMoveFiles &= QFile::rename(m_binPath + "cygcrypto-0.9.8.dll", m_binPath + "bin/cygcrypto-0.9.8.dll");
                bMustMoveFiles &= QFile::rename(m_binPath + "cygssp-0.dll", m_binPath + "bin/cygssp-0.dll");
                bMustMoveFiles &= QFile::rename(m_binPath + "cygz.dll", m_binPath + "bin/cygz.dll");
                bMustMoveFiles &= QFile::rename(m_binPath + "ssh.exe", m_binPath + "bin/ssh.exe");
            }
            m_binPath = m_binPath + "bin/";
        }
    }
}
#define LOG(global, local, x)   if (global)   gLog += x + QString("\r\n"); \
                                if (local)    log += x + QString("\r\n"); \
                                if (pDlg)     m_pLog->appendPlainText(x); \
                                if (pDlg)     m_pLog->moveCursor(QTextCursor::End);
                       
#define TRAY(x)                 if (m_pTrayIcon) m_pTrayIcon->setToolTip(QString(x))

//----------------------------------------------------------------------------
bool QtdSync::sendBackupReport(Config& config, QString log, bool bFailed)
{
    if (!config.sendReport || !config.mail.isValid() || (!bFailed && config.sendReportOnErrorOnly)) {
        return false;
    }

    // set mail subject
    config.mail.setMailSubject(QString("[QtdSync Backup Report] %1").arg(config.name));

    //------------------------------------------------------------------------
    // compose body

    QString body;

    body += QString("Backup Set             : %1\r\n").arg(config.fileName);
    body += QString(tr("Last successfull Backup") + ": %1\r\n").arg(config.lastBackup.toString());
    body += "\r\n";
    body += "--------------------------------------------------------------\r\n";
    body += "\r\n";
    body += log;

    config.mail.setMailBody(body);
    return config.mail.sendMail() == QtdMail::eNoError;
}

#define CONTINUE \
    if (!bRestore && !bDryRun) { \
        if (m_config.sendReport) { \
            LOG(true, false, "Sending E-Mail notification.\r\n"); \
            if (!sendBackupReport(m_config, log, !bSuccess)) { \
                LOG(true, false, "Sending backup report failed!\r\n"); \
            } \
        } \
        m_config.lastBackupTry = curDT; \
        m_config.unSaved = true; \
        m_config.bCurrentlyBackingUp = false; \
        saveQtd(); \
    } \
    continue

//----------------------------------------------------------------------------
void QtdSync::slot_abortBackup()
{
    if (!m_pProcess || m_pProcess->state() == QProcess::NotRunning) {
        if (m_pDlg && m_currentDlg == eMain) {
            m_pSW_Pages->setCurrentWidget(m_pPage_Client);
        } else {
            QWidget* pButton = qobject_cast<QWidget*>(sender());
            if (pButton) {
                QDialog* pDlg = qobject_cast<QDialog*>(pButton->window());
                if (pDlg) pDlg->accept();
            }
        }
        return;
    }
    m_bAborted = true;
    m_pProcess->close();
}
//----------------------------------------------------------------------------
bool QtdSync::doBackup(QStringList files, QMap<QString, QString> config, QMap<QString, QVariant>* pStats)
{
    QString backupFolder(config["backuplocation"]);
    QString restoreFolder = backupFolder;

    bool    useEverAfter = backupFolder != "";
    bool    override = config.contains("override")  && (config["override"]  == "true");
    bool    bSilent  = config.contains("silent")    && (config["silent"]    == "true");
    bool    bRestore = config.contains("restore")   && (config["restore"]   == "true");
    bool    bDryRun  = config.contains("dryrun")    && (config["dryrun"]    == "true");
    bool    bMarkFailedAsValid  = config.contains("markFailedAsValid") && (config["markFailedAsValid"]    == "true");
    bool    bScheduled = config.contains("scheduled") && (config["scheduled"] == "true");

    if (bDryRun) bSilent = true;

    unsigned long long nTotalFileSize = 0;
    unsigned long long nAllFilesSize = 0;

    bool    useFiles = files.count() > 0;
    bool    bSuccess = true;
    int     nPrio    = QtdTools::eProcessPriority_Normal;

    m_bAborted = false;

    if (m_settings.setProcessPrio) {
        nPrio = m_settings.processPrio;
    }

    QStringList excludeFolders;
    foreach (QString excl, config.keys()) {
        if (excl.startsWith("exclude=")) {
            excludeFolders << excl.mid(8);
        }
    }

    m_pProcess = new QProcess(this);
    m_pProcess->setWorkingDirectory(m_binPath);

    checkFstab();

    // store current config
    Config curConfig = m_config;
    m_config.isNew = false;
    m_config.unSaved = false;
    QStringList createdTempDirs;

    QString gLog(m_productName + " v" + m_productVersion + "\r\n");

    if (!useFiles) {
        files << m_config.fileName;
    }

    QDialog* pDlg = 0L;
    QDialog* pPrevDialog = 0L;
    if (!bSilent) {
        if (m_currentDlg != eMain) {
            pPrevDialog = m_pDlg;
            pDlg = new QDialog(m_pDlg, Qt::CustomizeWindowHint | Qt::WindowTitleHint);
            pDlg->setModal(true);
            this->SetupUi(pDlg);

            m_pL_ProductInfo->setText(QString("<b>%1 v%2</b><br/><small>%3</small>").arg(m_productName).arg(m_productVersion).arg(m_productCopyright));

            m_pDlg = pDlg;
        } else {
            pDlg = m_pDlg;
            pPrevDialog = m_pDlg;
        }

        m_pSW_Pages->setCurrentWidget(m_pPage_Backup);
        m_pLog->clear();

#ifndef WIN32
        m_pW_ProcessPriority->hide();
#endif
        connect(m_pClose, SIGNAL(clicked()), this, SLOT(slot_abortBackup()));

        m_pGlobalProgress->setRange(0, files.count());
        pDlg->show();
        m_pProgressBarFile->setRange(0,100);

        qApp->processEvents();
    }

    int count = 0;

    if (pDlg) m_pGlobalProgress->setValue(m_pGlobalProgress->maximum());
    qApp->processEvents();
    bool bGlobalSuccess = true;
    QString currentBackupDest("");
    QString currentSpeedString("0.00kB/s");
    QString sshPath = PEXEC("ssh");
    QString sshPassPath = PEXEC("sshpass");
    sshPath.replace("/", "\\");
    sshPassPath.replace("/", "\\");

    TRAY(tr("Initializing backup..."));


    QMap<QString, QPair<QDateTime, QString> > gatheredErrors;

    for (int nFile = 0; nFile < files.count(); nFile++) {
        bool    bForce   = config.contains("force")     && (config["force"]     == "true");
        currentBackupDest = "";
        QString fileName = files.at(nFile);
        count++;
        bGlobalSuccess &= bSuccess;
        if (pDlg) m_pGlobalProgress->setRange(0,0);

        QDateTime curDT = QDateTime::currentDateTime();

        QString log(m_productName + " v" + m_productVersion + "\r\n");
        if (useFiles) {
            if (!openQtd(fileName)) {
                LOG(true, false, tr("Backup set %1 invalid!").arg(fileName));
                bSuccess = false;
                continue;
            }
        }

        if (m_config.destination.bRemote && bDryRun) {
            LOG(true, false, tr("Dry run not allowed for remote backups (%1)!").arg(fileName));
            bSuccess = false;
            continue;
        }

        if (m_config.bCurrentlyBackingUp) {
            if (!bSilent && !bForce) {
                if (QMessageBox::question(0L, tr("Currently backing up"), tr("The backup set <b>%1</b> is marked as <i>currently backing up</i>!<br><br>It is likely that another QtdSync instance (e.g. QtdSync Monitor) is currently performing a backup.<br><br>However, do you want to force QtdSync to backup now?").arg(m_config.name), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
                   bForce = true; 
                }
            }
            if (!bForce) {
                LOG(true, true, tr("The Backup Set %1 is already currently performed!").arg(m_config.name));
                continue;
            }
        }

        if (!bDryRun && !bRestore) {
            m_config.bCurrentlyBackingUp = true;
            saveQtd();
        }

        if (m_config.destination.bRemote && m_pTrayIcon) {
            m_pTrayIcon->setIcon(QIcon(isServerRunning() ? ":/images/schedule_remote_server.png" : ":/images/schedule_remote.png"));
        }

        currentBackupDest = restoreFolder = evaluateString(m_config.destination.dest);
        if (m_config.destination.bRemote) {
            currentBackupDest.replace(" ", "_");
            restoreFolder.replace(" ", "_");
        }

        if (m_config.name == "") m_config.name = QFileInfo(fileName).baseName();

        // check is backup set is bound to any other computer
        if (currentBackupDest.length() > 2 && currentBackupDest.at(1) == '{') {
            LOG(true, true, tr("Backup set %1 is bound to another computer!").arg(m_config.name));
            CONTINUE;
        }

        if (bScheduled) {
            nPrio = m_config.scheduledPrio;
        }

        if (pDlg && m_pS_ProcessPrio) {
            m_pS_ProcessPrio->setValue(nPrio);
        }
        qApp->processEvents();


        QString passwd("");
        if (m_config.destination.bRemote) {
            if (m_config.destination.password == "" && m_config.destination.bUserAuth) {
                if (!bSilent) {
                    bool bShowBindings = m_settings.showFolderBindingOptions;
                    m_settings.showFolderBindingOptions = false;
                    qApp->processEvents();
                    if (pDlg) {
                        pDlg->setModal(false);
                    }
                    m_config.destination.password = "%%QTDSYNCING%%";
                    slot_setRemoteUserAuth();

                    qApp->processEvents();
                    m_settings.showFolderBindingOptions = bShowBindings;
                    passwd = m_config.destination.password;
                    m_config.destination.password = "";
                } else {
                    LOG(true, true, tr("User settings invalid (empty password)!"));
                    bSuccess = false;
                    CONTINUE;
                }
            }

            QStringList env = m_pProcess->environment();

            if (passwd == "") passwd = m_config.destination.password;
            if (((passwd == "" && m_config.destination.bUseSSH) || passwd == "%%QTDSYNCABORTED%%") && m_config.destination.bRemote) {
                LOG(true, true, tr("Backup aborted by user."));
                CONTINUE;
            }
            if (passwd == "") passwd = "dummypw";
            QStringList newEnv;
            foreach (QString e, env) {
                if (!e.startsWith("RSYNC_PASSWORD=") && !e.startsWith("SSHPASS=")) {
                    newEnv << e;
                }
            }
            if (!m_config.destination.bUseSSH) {
                newEnv << "RSYNC_PASSWORD="+passwd;
            } else {
                newEnv << "SSHPASS="+passwd;
            }
            m_pProcess->setEnvironment(newEnv);
        }


        if (pDlg) m_pL_BackupSet->setText((bRestore ? tr("Restoring") + " " : "") + m_config.name);
        LOG(true, false, tr("Doing backup set %1").arg(m_config.name));
        LOG(true, false, "=====================================");
        TRAY(tr("Initializing backup for %1...").arg(m_config.name));
        QString trayHeader("");

        if (override) {
            if (!bRestore) {
                currentBackupDest = backupFolder;
            } else {
                restoreFolder = backupFolder;
            }
        }

        if (currentBackupDest == "" || (!m_config.destination.bRemote && !QDir(currentBackupDest).exists())) {
            if (backupFolder == "" || !QDir(backupFolder).exists() || !useEverAfter) {
                if (!bSilent) {
                    backupFolder = QFileDialog::getExistingDirectory(m_pDlg, tr("QtdSync backup destination for \"%1\"").arg(m_config.name), "");
                    if (backupFolder == "") {
                        if (QMessageBox::question(m_pDlg, tr("Abort Backup"), tr("Abort Backup") + "?", QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes) {
                            LOG(true, true, tr("Backup aborted by user."));
                            bSuccess = false;
                            if (!bDryRun) {
                                m_config.lastBackupTry = curDT;
                                m_config.unSaved = true;
                                saveQtd();
                            }
                            if (!bRestore) sendBackupReport(m_config, log, true);
                            goto endbackup;
                        } else {
                            LOG(true, true, tr("Skip backup set %1").arg(m_config.name));
                            bSuccess = false;
                            CONTINUE;
                        }
                    }
                    useEverAfter = files.count() > 1 ? QMessageBox::question(m_pDlg, tr("Store destination"), tr("Use backup destination <b>%1</b> for every following undefined backup destination?").arg(backupFolder), QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes : true;
                    currentBackupDest = backupFolder;
                } else {
                    LOG(true, true, tr("Backup destination %1 does not exist!").arg(backupFolder));
                    bSuccess = false;
                    if (!bDryRun) {
                        m_config.lastBackupTry = curDT;
                        m_config.unSaved = true;
                        saveQtd();
                    }
                    if (!bRestore) sendBackupReport(m_config, log, true);
                    goto endbackup;
                }
            } else if (useEverAfter && QDir(backupFolder).exists()) {
                currentBackupDest = backupFolder;
            }
        }

        trayHeader = m_config.name + " (" + currentBackupDest + ")";

        QString lastBackup("");
        QString newDir(""), newDir2(""), newDirOrig("");
        QString createDir("");
        bool bCreateMain = true;

        QString configDir = m_config.name;
        QString configDirPath;
        if (m_config.destination.bRemote) {
            configDir.replace(" ", "_");
        }
        configDirPath = "/" + configDir;
        if (!m_config.bCreateSubfolder) {
            configDir = "";
            configDirPath = "";
        }

        // if doing remote backup, we have to create the folder first
        if (m_config.destination.bRemote && !bRestore) {
            createDir = configDir;
            if (QDir(QDir::temp().absolutePath() + "/QtdSync_" + curDT.toString("yyyy.MM.dd_hh.mm.ss")).exists()) {
                bCreateMain = false;
            }
            if (bCreateMain && !QDir::temp().mkdir("QtdSync_" + curDT.toString("yyyy.MM.dd_hh.mm.ss"))) {
                bSuccess = false;
            } else if (m_config.bCreateSubfolder && !QDir(QDir::temp().absolutePath() + "/QtdSync_" + curDT.toString("yyyy.MM.dd_hh.mm.ss")).mkdir(configDir)) {
                bSuccess = false;
            }

            if (bSuccess) {
                createdTempDirs << QDir::temp().absolutePath() + "/QtdSync_" + curDT.toString("yyyy.MM.dd_hh.mm.ss");
            }

            if (!bSuccess) {
                LOG(true, true, tr("Fatal error creating temporary directory %1!").arg(createDir));
                CONTINUE;
            }
        }

        if (!m_config.destination.bRemote && !bRestore && m_config.bCreateSubfolder) {
            if (!QDir(currentBackupDest + configDirPath).exists() && !bDryRun) {
                if (!QDir(currentBackupDest).mkdir(configDir)) {
                    LOG(true, true, tr("Fatal error creating directory %1!").arg(currentBackupDest + configDirPath));
                    bSuccess = false;
                    CONTINUE;
                }
            }
        }

        if (m_config.type == eDifferential) {
            // get last backup destination
            if (bRestore) {
                if (m_config.destination.bRemote) {
                    lastBackup = currentBackupDest + QString("%1%2/%3%4").arg(m_config.destination.bUseSSH ? ":" : "/").arg(m_config.destination.path).arg(m_config.bCreateSubfolder ? configDir + "/" : QString("")).arg(m_config.lastBackup.toString("yyyy.MM.dd_hh.mm.ss"));
                } else {
                    QString sLast;
                    QStringList lDirs = QDir(currentBackupDest + configDirPath).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                    lDirs.sort();
                    for (int i = lDirs.count()-1; i >= 0; i--) {
                        sLast = lDirs.at(i);
                        if (QDateTime::fromString(sLast, "yyyy.MM.dd_hh.mm.ss").isValid()) {
                            break;
                        } else {
                            sLast = "";
                        }
                    }
                    if (sLast != "") {
                        lastBackup = currentBackupDest + configDirPath + "/" + sLast;
                    } else {
                        lastBackup = currentBackupDest + configDirPath + "/" + m_config.lastBackup.toString("yyyy.MM.dd_hh.mm.ss");
                    }
                }
            } else {
                QString sLast;
                if (!m_config.destination.bRemote) {
                    QStringList lDirs = QDir(currentBackupDest + configDirPath).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                    lDirs.sort();

                    for (int i = lDirs.count()-1; i >= 0; i--) {
                        sLast = lDirs.at(i);
                        if (QDateTime::fromString(sLast, "yyyy.MM.dd_hh.mm.ss").isValid()) {
                            break;
                        } else {
                            sLast = "";
                        }
                    }
                }
                if (sLast != "") {
                    lastBackup = "../" + sLast;
                } else {
                    lastBackup = "../" + m_config.lastBackup.toString("yyyy.MM.dd_hh.mm.ss");
                }
            }
            //new backup destination
            newDir = curDT.toString("yyyy.MM.dd_hh.mm.ss");

            if (!m_config.destination.bRemote && !bRestore) {
                if (!bDryRun) {
                    if (!QDir(currentBackupDest + configDirPath).mkdir(newDir)) {
                        LOG(true, true, tr("Fatal error creating directory %1!").arg(newDir));
                        bSuccess = false;
                        CONTINUE;
                    }
                    newDir = currentBackupDest + configDirPath + "/" + newDir;
                } else {
                    newDir = currentBackupDest + "/QtdSync_dryrun_tmp";
                }
            } else if (!bRestore) {
                if (!QDir(QDir::temp().absolutePath() + "/QtdSync_" + curDT.toString("yyyy.MM.dd_hh.mm.ss") + configDirPath).mkdir(curDT.toString("yyyy.MM.dd_hh.mm.ss"))) {
                    bSuccess = false;
                    LOG(true, true, tr("Fatal error creating directory %1!").arg(configDirPath + "/" + curDT.toString("yyyy.MM.dd_hh.mm.ss")));
                    CONTINUE;
                } else {
                    createDir += (createDir != "" ? "/" : "") + curDT.toString("yyyy.MM.dd_hh.mm.ss");
                }
                newDir = currentBackupDest + QString("%1").arg(m_config.destination.bUseSSH ? ":" : "/") + m_config.destination.path + configDirPath + "/" + curDT.toString("yyyy.MM.dd_hh.mm.ss");
            }
        } else /* if (m_config.type == eSynchronize) */{
            if (!m_config.destination.bRemote) {
                newDir = currentBackupDest + configDirPath;
            } else {
                newDir = currentBackupDest + QString("%1").arg(m_config.destination.bUseSSH ? ":" : "/") + m_config.destination.path + configDirPath;
            }
            if (bRestore) {
                if (m_config.destination.bRemote) {
                    lastBackup = currentBackupDest + QString("%1").arg(m_config.destination.bUseSSH ? ":" : "/") + m_config.destination.path + configDirPath;
                } else {
                    lastBackup = currentBackupDest + configDirPath;
                }
            }
        }

        // do preprocessing
        if (!bRestore && m_config.preProcessing.first != "" && !bDryRun) {
            LOG(true, true, "");
            LOG(true, true, tr("Doing backup preprocessing..."));
            LOG(true, true, "-------------------------------------");

            bool bDoIt = true;
            QString evalFile = evaluateString(m_config.preProcessing.first);
            if (!QFileInfo(evalFile).exists()) {
                LOG(true, true, "    " + tr("%1 not found!").arg(evalFile));
                if (!bSilent) {
                    bDoIt = (QMessageBox::question(0L, tr("File not found"), 
                                                   tr("The backup set preprocessing executable<br><br><b>%1</b><br><br>does not exists!<br><br>Abort backup?").arg(evalFile),
                                                   QMessageBox::Yes | QMessageBox::No) == QMessageBox::No);
                } else {
                    bDoIt = false;
                }

                if (!bDoIt) {
                    LOG(true, true, tr("Backup aborted!"));
                    bSuccess = false;
                    CONTINUE;
                }
            } else {
                TRAY(trayHeader + "\r\n" + tr("Preprocessing") + " (" + evalFile + ")");

                LOG(true, true, "    " + tr("%1 %2").arg(evalFile).arg(m_config.preProcessing.second));
                QProcess* preProcess = new QProcess();
                preProcess->setWorkingDirectory(QFileInfo(evalFile).absolutePath());
                preProcess->start(evalFile, QtdTools::getCommandLineArguments(m_config.preProcessing.second));
                preProcess->waitForFinished(-1);
                QString out = preProcess->readAllStandardOutput();
                out.replace("\r\n", "\n");
                LOG(true, true, out);
                if (preProcess->exitCode() != 0) {
                    QString errStr = preProcess->readAllStandardError();
                    bool bAbort = bSilent;
                    if (!bAbort) {
                        QMessageBox* pBox = new QMessageBox(0L);
                        pBox->setIcon(QMessageBox::Warning);
                        pBox->setText(tr("The preprocessing returned with an error."));
                        pBox->setInformativeText(tr("Do you want to abort the backup?"));
                        if (errStr != "") {
                            pBox->setDetailedText(errStr);
                        }
                        pBox->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                        pBox->setDefaultButton(QMessageBox::No);

                        bAbort = (pBox->exec() == QMessageBox::Yes);
                        delete pBox;
                    }
                    if (bAbort) {
                        LOG(true, true, tr("Preprocessing returned with an error:"));
                        LOG(true, true, errStr);
                        LOG(true, true, "Backup aborted!");
                        bSuccess = false;
                        CONTINUE;
                    }
                }
            }
            LOG(true, true, "-------------------------------------");
            LOG(true, true, "");
        }

        if (m_config.destination.bRemote) {
            LOG(true, true, tr("Connecting to %1.").arg(m_config.destination.dest)+"..");
            qApp->processEvents();
            TRAY(trayHeader + "\r\n" + tr("Connecting to %1.").arg(m_config.destination.dest)+"..");
        }

        int nPort = -1;
        if (m_config.destination.bRemote && createDir != "") {
            // create dir on remote server
            QString sourceDir = QDir::temp().absolutePath() + "/QtdSync_" + curDT.toString("yyyy.MM.dd_hh.mm.ss") + configDirPath;
            sourceDir.replace("\\","/");
#ifdef WIN32
            if (sourceDir.at(1) == ':') {
                sourceDir = "/cygdrive/" + QString(sourceDir.at(0)) + sourceDir.mid(2);
            }
#endif
            QString destDir = currentBackupDest + QString("%1").arg(m_config.destination.bUseSSH ? ":" : "/") + m_config.destination.path + configDirPath;
            if (m_config.destination.bUserAuth) {
                destDir = m_config.destination.user + "@" + destDir;
            }
            if (!m_config.destination.bUserAuth || !m_config.destination.bUseSSH) {
                destDir = "rsync://" + destDir;
            }

            QStringList rsyncArgs("-rltv");
            if (m_config.destination.bUseSSH && m_config.destination.bRemote && destDir.count(":") > 1) {
                QStringList destParts = destDir.split(":");
                nPort = destParts.at(1).toInt();
                destDir = destParts.at(0) + ":" + destParts.at(2);
            }
            if (m_config.destination.bUseSSH) {
                if (!m_config.destination.bUsePassword) {
                    rsyncArgs << "-e" << QString("\"" + sshPath + "\" %1-i \"%2\" -o StrictHostKeyChecking=no -o PreferredAuthentications=hostbased,publickey -o NumberOfPasswordPrompts=0").arg(nPort != -1 ? QString("-p %1 ").arg(nPort) : "").arg(evaluateString(passwd));
                } else {
                    rsyncArgs << "-e" << QString("\"" + sshPassPath + "\" -e \"" + sshPath + "\" %1-o StrictHostKeyChecking=no -o NumberOfPasswordPrompts=1").arg(nPort != -1 ? QString("-p %1 ").arg(nPort) : "");
                }
            }

#ifdef WIN32
            rsyncArgs << "--exclude=/cygdrive";
#endif
            rsyncArgs << "--exclude=/proc";
            rsyncArgs << sourceDir + "/";
            rsyncArgs << destDir + "/";

            if (m_bDebug || m_settings.showRsyncExpertSettings) {
                QString strProgram = QString("    %1%2\r\n").arg(EXEC("rsync")).arg("\r\n        \"" + rsyncArgs.join("\"\r\n        \"") + "\"");
                LOG(true, true, QString("rsync command line:\r\n") + strProgram);
            }

            m_pProcess->start(PEXEC("rsync"), rsyncArgs);
            m_pProcess->waitForStarted(-1);
            QtdTools::setProcessPriority(m_pProcess, (QtdTools::ProcessPriority)nPrio);
            while (m_pProcess->state() != QProcess::NotRunning) {
                qApp->processEvents();
                m_pProcess->waitForReadyRead(100);
            }


            if (m_pProcess->exitCode() != 0) {
                bSuccess = false;
            }

            QDir tmp(QDir::temp().absolutePath() + "/" + curDT.toString("yyyy.MM.dd_hh.mm.ss"));
            QtdTools::removeDirectory(tmp);
            if (!bSuccess) {
                QString errs = m_pProcess->readAllStandardError();
                if (errs.toLower().contains("auth failed")) {
                    LOG(true, true, tr("User authentication failed!"));
                    CONTINUE;
                } else if (errs.toLower().contains("permission denied")) {
                    LOG(true, true, tr("Permission denied!"));
                    CONTINUE;
                } else {
                    LOG(true, true, errs);
                }
            }
        }

        newDirOrig = newDir;
        newDir.replace("\\","/");

        if (bRestore) {
            newDir = restoreFolder;
        }
        if (!m_config.destination.bRemote || bRestore) {
#ifdef WIN32
            if (newDir.at(1) == ':') {
                newDir = "/cygdrive/" + QString(newDir.at(0)) + newDir.mid(2);
            }
#endif
        } else {
            if (m_config.destination.bUserAuth) {
                newDir = m_config.destination.user + "@" + newDir;
            }
            if (!m_config.destination.bUserAuth || !m_config.destination.bUseSSH) {
                newDir = "rsync://" + newDir;
            }
        }
        QString lastBackupOrig = lastBackup;

        if (lastBackup != "" && bRestore) {
            lastBackup.replace("\\", "/");
            if (!m_config.destination.bRemote) {
#ifdef WIN32
                if (lastBackup.at(1) == ':') {
                    lastBackup = "/cygdrive/" + QString(lastBackup.at(0)) + lastBackup.mid(2);
                }
#endif
            } else {
                if (m_config.destination.bUserAuth) {
                    lastBackup = m_config.destination.user + "@" + lastBackup;
                }
                if (!m_config.destination.bUserAuth || !m_config.destination.bUseSSH) {
                    lastBackup = "rsync://" + lastBackup;
                }
            }
        } else if (bRestore) {
            LOG(true, true, tr("Backup folder unknown!"));
            bSuccess = false;
            CONTINUE;
        }

        if (pDlg) m_pGlobalProgress->setRange(0, m_config.folders.count());
        bool failure = false;
        QString curFolder("");
        for (int k = 0; k < m_config.folders.count(); k++) {
            int rangeMax = 0;
            QString folderDir = m_config.folders.at(k).name;
            QString folder = evaluateString(m_config.folders.at(k).folder);
            
            QString curLastBackup = lastBackup;
            if (m_config.destination.bRemote) {
                folderDir.replace(" ", "_");
            }
            QString folderDirPath = "/" + folderDir;

            if (!bRestore && !m_config.folders.at(k).bCreateSubFolder && m_config.folders.count() <= 1) {
                folderDirPath = "";
            } else {
                curLastBackup = "../" + lastBackup;
            }

            if (pDlg) m_pProgressBar->setRange(0, rangeMax);

            if (excludeFolders.contains(folderDir)) {
                CONTINUE;
            }

            // check if folder is bound to any other computer
            if (folder.length() > 2 && folder.at(1) == '{') {
                LOG(true, true, tr("Folder %1 is bound to another computer!").arg(folder));
                CONTINUE;
            }

            LOG(true, true, "");
            LOG(true, true, folder);
            LOG(true, true, "-------------------------------------");
            if (!QDir(folder).exists()) {
                LOG(true, true, tr("Folder %1 does not exists. Skip!").arg(folder));
                bSuccess = false;
                CONTINUE;
            }
            if (m_config.destination.bRemote) {
                LOG(true, false, tr("Please wait..."));
            }

            if (pDlg) m_pL_Folder->setText(folder);
            curFolder = folder;
            if (curFolder.endsWith("/")) curFolder = curFolder.mid(0, curFolder.length()-1);

            folder.replace("\\", "/");
#ifdef WIN32
            if (folder.at(1) == ':') {
                folder = "/cygdrive/" + QString(folder.at(0)) + folder.mid(2);
            }
#endif
            QStringList rsyncArgs("-rltv");
            rsyncArgs << "--progress";
            if (bDryRun) {
                rsyncArgs << "--dry-run";
                rsyncArgs << "--stats";
            }
            QString rsyncExperts = m_config.folders.at(k).rsyncExpert;
            rsyncExperts.replace("use-global-settings", m_config.globalRsyncOptions);
            rsyncArgs << rsyncExperts.split(" ", QString::SkipEmptyParts);

            if (!rsyncArgs.contains("--whole-file") && !rsyncArgs.contains("--no-whole-file")) {
                bool bAddNoWholeFile = m_config.destination.bRemote;
                if (!bAddNoWholeFile) {
                    QtdTools::DriveType driveType = QtdTools::getDriveType(currentBackupDest.toLatin1().data()[0]);
                    bAddNoWholeFile |= (driveType != QtdTools::eDrive_Fixed && driveType != QtdTools::eDrive_RamDisk);
                }
                if (bAddNoWholeFile) {
                    rsyncArgs << "--no-whole-file";
                }
            }

            if (m_config.destination.bRemote) {
                rsyncArgs << "-z";
            }
            if (m_config.destination.bUseSSH) {
                if (!m_config.destination.bUsePassword) {
                    rsyncArgs << "-e" << QString("\"" + sshPath + "\" %1-i \"%2\" -o StrictHostKeyChecking=no -o PreferredAuthentications=hostbased,publickey -o NumberOfPasswordPrompts=0").arg(nPort != -1 ? QString("-p %1 ").arg(nPort) : "").arg(evaluateString(passwd));
                } else {
                    rsyncArgs << "-e" << QString("\"" + sshPassPath + "\" -e \"" + sshPath + "\" %1-o StrictHostKeyChecking=no -o NumberOfPasswordPrompts=1").arg(nPort != -1 ? QString("-p %1 ").arg(nPort) : "");
                }
            }
            if (lastBackup != "" || m_config.type == eSynchronize) {
                //rsyncArgs << "--delete";
                //rsyncArgs << "--force";
                if (!bRestore) {
                    if (m_config.type != eSynchronize) {
                        rsyncArgs.append(QString("--link-dest=%1%2").arg(curLastBackup).arg(folderDirPath));
                    } else {
                        if (!bDryRun) {
                            rsyncArgs.append(QString("--link-dest=."));
                        } else {
                            rsyncArgs.append(QString("--link-dest=%1").arg(newDir + folderDirPath));
                        }
                    }
                }
            }

            if (m_config.folders.at(k).exclude != "" && !bRestore) {
                QStringList excludes = m_config.folders.at(k).exclude.split(";");
                foreach (QString ex, excludes) {
                    if (!ex.startsWith("/")) {
                        QString::iterator it = ex.begin();
                        for (it; it != ex.end(); it++) {
                            unsigned char t = it->toAscii();
                            if (t > char(127)) {
                                *it = QChar('*');
                            }
                        }
                        rsyncArgs << "--exclude=" + ex;
                    }
                }
            }
            if (!bRestore) foreach (QString exFile, m_config.folders.at(k).filesExcluded) {
                if (exFile != "/") {
                    QString::iterator it = exFile.begin();
                    for (it; it != exFile.end(); it++) {
                        unsigned char t = it->toAscii();
                        if (t > char(127)) {
                            *it = QChar('*');
                        }
                    }
                    rsyncArgs << "--exclude=" + exFile;
                }
            }

#ifdef WIN32
            rsyncArgs << "--exclude=/cygdrive";
#endif
            rsyncArgs << "--exclude=/proc";
            int nPort = -1;
            if (!bRestore) {
                rsyncArgs.append(folder + "/");
                if (m_config.destination.bUseSSH && m_config.destination.bRemote && newDir.count(":") > 1) {
                    QStringList destParts = newDir.split(":");
                    nPort = destParts.at(1).toInt();
                    newDir = destParts.at(0) + ":" + destParts.at(2);
                }

                if (bDryRun && m_config.type) {
                    if (!QDir(currentBackupDest).mkdir("QtdSync_dryrun_tmp")) {
                        LOG(true, true, tr("Folder creation of %1 failed!").arg(currentBackupDest + "/QtdSync_dryrun_tmp"));
                        bSuccess = false;
                        CONTINUE;
                    } else {
                        createdTempDirs << currentBackupDest + "/QtdSync_dryrun_tmp";
                    }
                    rsyncArgs.append(newDir  + "/QtdSync_dryrun_tmp");
                } else {
                    rsyncArgs.append(newDir + folderDirPath);
                }
            } else {
                int nPort = -1;
                if (m_config.destination.bUseSSH && m_config.destination.bRemote && lastBackup.count(":") > 1) {
                    QStringList destParts = lastBackup.split(":");
                    nPort = destParts.at(1).toInt();
                    lastBackup = destParts.at(0) + ":" + destParts.at(1);
                }
                if (bRestore && override) {
                    QString src = QString("%1%2").arg(lastBackup).arg(folderDirPath);
                    rsyncArgs.append(src);
                    rsyncArgs.append(newDir);
                } else {
                    QString src = QString("%1%2/").arg(lastBackup).arg(folderDirPath);
                    rsyncArgs.append(src);
                    rsyncArgs.append(folder);
                }
            }

            qApp->processEvents();

            if (pDlg) m_pClose->setEnabled(true);
            if (pDlg) m_pClose->setIcon(QIcon(":/images/cancel.png"));
            if (m_pTrayMenu && m_pAbortBackup) m_pAbortBackup->setEnabled(true);

            if (m_bDebug || m_settings.showRsyncExpertSettings) {
                QString strProgram = QString("    %1%2\r\n").arg(EXEC("rsync")).arg("\r\n        \"" + rsyncArgs.join("\"\r\n        \"") + "\"");
                LOG(true, true, QString("rsync command line:\r\n") + strProgram);
            }
            m_pProcess->start(PEXEC("rsync"), rsyncArgs);
            m_pProcess->waitForStarted(-1);
            QtdTools::setProcessPriority(m_pProcess, (QtdTools::ProcessPriority)nPrio);
            int nWaitForReady = 100;
            int nCurCount = 0;
            QString curFile("");
            while (m_pProcess->state() != QProcess::NotRunning) {
                qApp->processEvents();

                if (pDlg && m_pS_ProcessPrio && m_pS_ProcessPrio->value() != nPrio) {
                    nPrio = m_pS_ProcessPrio->value();
                    QtdTools::setProcessPriority(m_pProcess, (QtdTools::ProcessPriority)nPrio);
                }

                if (m_pProcess->waitForReadyRead(nWaitForReady)) {
                    //nWaitForReady = -1;
                    QString out = QString::fromUtf8(m_pProcess->readAllStandardOutput());
                    QString errs = m_pProcess->readAllStandardError();
                    if (m_config.destination.bRemote && errs.toLower().contains("auth failed")) {
                        LOG(true, true, "---> " + tr("User authentication failed!"));
                    } else if (m_config.destination.bRemote && errs.toLower().contains("auth failed")) {
                        LOG(true, true, "---> " + tr("User authentication failed!"));
                    }
                    QStringList outLines = out.split("\n");
                   
                    foreach (QString outLine, outLines) {
                        qApp->processEvents();
                        //LOG(true, true, outLine);
                        outLine = outLine.replace("\r", "");
                        if (outLine == "") continue;
                        if (!outLine.startsWith(" ") || m_bDebug) {
                            LOG(true, true, outLine.replace("\r", ""));
                            if (pDlg) m_pL_Folder->setText(outLine);
                        }
                        QRegExp regExp("to-check=(\\d+)/(\\d+)");
                        if (regExp.indexIn(outLine) != -1 && regExp.numCaptures() > 1) {
                            if (rangeMax != regExp.cap(2).toInt()) {
                                if (pDlg) m_pProgressBar->setMaximum(rangeMax = regExp.cap(2).toInt());
                                if (pDlg) m_pProgressBar->setFormat("%v/%m");
                            }
                            if (pDlg) m_pProgressBar->setValue(nCurCount = (rangeMax - regExp.cap(1).toInt()));
                        }
                        regExp.setPattern("\\s(\\d+)%\\s");
                        if (regExp.indexIn(outLine) != -1 && regExp.numCaptures() > 0) {
                            if (pDlg) m_pProgressBarFile->setValue(regExp.cap(1).toInt());
                        }

                        regExp.setPattern("Total\\stransferred\\sfile\\ssize:\\s(\\d+)\\sbytes");
                        if (regExp.indexIn(outLine) != -1 && regExp.numCaptures() > 0) {
                            nTotalFileSize += regExp.cap(1).toULongLong() / 1024;
                        }

                        regExp.setPattern("Total\\sfile\\ssize:\\s(\\d+)\\sbytes");
                        if (regExp.indexIn(outLine) != -1 && regExp.numCaptures() > 0) {
                            nAllFilesSize += regExp.cap(1).toULongLong() / 1024;
                        }

                        regExp.setPattern("\\s([\\.\\d]+[k|M|G]B/s)\\s");
                        if (regExp.indexIn(outLine) != -1 && regExp.numCaptures() > 0) {
                            currentSpeedString = regExp.cap(1);
                            if (pDlg) m_pL_Speed->setText("<b>" + currentSpeedString + "</b>");
                        }

                        if (!outLine.startsWith(" ") && QFileInfo(curFolder+"/"+outLine.replace("\r", "")).exists()) {
                            curFile = curFolder+"/"+outLine.replace("\r", "");
                        }

                        TRAY(trayHeader + " - " + currentSpeedString + "\r\n" + curFile);
                    }
                }
            }

            if (pDlg) m_pClose->setEnabled(false);
            if (m_pTrayMenu && m_pAbortBackup) m_pAbortBackup->setEnabled(false);

            qApp->processEvents();

            bSuccess = !(failure |= m_pProcess->exitCode() != 0);
            if (failure && m_bAborted) {
                LOG(true, true, "---> " + tr("Backup canceled!"));
                goto endbackup;
            }
            if (failure) {
                QString errs = m_pProcess->readAllStandardError();
                if (m_config.destination.bRemote && errs.toLower().contains("auth failed")) {
                    LOG(true, true, "---> " + tr("User authentication failed!"));
                } else if (m_config.destination.bRemote && errs.toLower().contains("connection timed out")) {
                    LOG(true, true, "---> " + tr("Running rsync failed! Check server settings!"));
                } else {
                    if (bSilent && !bMarkFailedAsValid) {
                        LOG(true, true, errs);
                    } else if (bSilent && bMarkFailedAsValid) {
                        failure = !(bSuccess = true);
                    } else {
                        QStringList errList = errs.split("\n", QString::SkipEmptyParts);
                        errs = errList.join("\n\n");
                        gatheredErrors[fileName] = QPair<QDateTime, QString>(curDT, errs);
                        failure = !(bSuccess = false);
                    }

                    if (failure) {
                        LOG(true, true, "---> " + tr("Running rsync failed!"));
                    }
                }
            }
            QString out = QString::fromUtf8(m_pProcess->readAllStandardOutput());

            if (!bRestore && bDryRun) {
                QDir tmp(currentBackupDest + "/QtdSync_dryrun_tmp");
                QtdTools::removeDirectory(tmp);
            }

            // filter output, again
            QStringList outLines = out.split("\n");
            foreach (QString outLine, outLines) {
                qApp->processEvents();
                //LOG(true, true, outLine);
                outLine = outLine.replace("\r", "");
                if (outLine == "") continue;
                if (!outLine.startsWith(" ") || m_bDebug) {
                    LOG(true, true, outLine.replace("\r", ""));
                    if (pDlg) m_pL_Folder->setText(outLine);
                }
                QRegExp regExp("to-check=(\\d+)/(\\d+)");
                if (regExp.indexIn(outLine) != -1 && regExp.numCaptures() > 1) {
                    if (rangeMax != regExp.cap(2).toInt()) {
                        if (pDlg) m_pProgressBar->setMaximum(rangeMax = regExp.cap(2).toInt());
                    }
                    if (pDlg) m_pProgressBar->setValue(nCurCount = (rangeMax - regExp.cap(1).toInt()));
                }
                regExp.setPattern("\\s(\\d+)%\\s");
                if (regExp.indexIn(outLine) != -1 && regExp.numCaptures() > 0) {
                    if (pDlg) m_pProgressBarFile->setValue(regExp.cap(1).toInt());
                }
                regExp.setPattern("Total\\stransferred\\sfile\\ssize:\\s+(\\d+)\\s+bytes");
                if (regExp.indexIn(outLine) != -1 && regExp.numCaptures() > 0) {
                    nTotalFileSize += regExp.cap(1).toULongLong() / 1024;
                }

                regExp.setPattern("Total\\sfile\\ssize:\\s+(\\d+)\\s+bytes");
                if (regExp.indexIn(outLine) != -1 && regExp.numCaptures() > 0) {
                    nAllFilesSize += regExp.cap(1).toULongLong() / 1024;
                }
            }

            int nCountDiff = rangeMax - nCurCount;

            if (pDlg) m_pGlobalProgress->setValue(k+1);
        }

        // do postprocessing
        if (!bRestore && m_config.postProcessing.first != "" && !bDryRun) {
            LOG(true, true, "");
            LOG(true, true, tr("Doing backup postprocessing..."));
            LOG(true, true, "-------------------------------------");
            QString evalFile = evaluateString(m_config.postProcessing.first);
            if (!QFileInfo(evalFile).exists()) {
                LOG(true, true, "    " + tr("%1 not found!").arg(evalFile));
                if (!bSilent) {
                    QMessageBox::warning(0L, tr("File not found"), tr("The backup set postprocessing executable<br><br><b>%1</b><br><br>does not exists!"));
                }
            } else {
                LOG(true, true, "    " + tr("%1 %2").arg(evalFile).arg(m_config.postProcessing.second));
                QProcess* preProcess = new QProcess();
                preProcess->setWorkingDirectory(QFileInfo(evalFile).absolutePath());
                preProcess->start(evalFile, QtdTools::getCommandLineArguments(m_config.postProcessing.second));
                preProcess->waitForFinished(-1);
                QString out = preProcess->readAllStandardOutput();
                out.replace("\r\n", "\n");
                LOG(true, true, out);
                if (preProcess->exitCode() != 0) {
                    QString errStr = preProcess->readAllStandardError();
                    errStr = errStr.split("\n", QString::SkipEmptyParts).join("</li><li>");
                    if (errStr != "") {
                        errStr = "<ul><li>" + errStr + "</li><ul>";
                    }
                    if (!bSilent) {
                        QMessageBox::warning(0L, tr("Postprocessing error"), tr("The postprocessing returned with an error. %1").arg(errStr));
                    }
                }
            }
            LOG(true, true, "-------------------------------------");
        }

        if (pDlg) m_pProgressBar->setMaximum(100);
        if (pDlg) m_pProgressBar->setValue(100);
        bGlobalSuccess &= bSuccess;

        if (!bDryRun) {
            if (bSuccess) {
                if (!bRestore) {
                    m_config.lastBackupTry = curDT;
                    m_config.lastBackup = curDT;
                    m_config.unSaved = true;
                    m_config.bCurrentlyBackingUp = false;
                    saveQtd();
                }
                if (m_config.fileName == curConfig.fileName) {
                    curConfig.lastBackupTry = curDT;
                    curConfig.lastBackup = curDT;
                }
            }
            if (!bRestore) if (sendBackupReport(m_config, log, !bSuccess)) {
                LOG(true, true, tr("Report mail sent to %1.").arg(m_config.mail.receiverAddress()) + "\r\n");
            }

            QFile file;
            if (m_config.destination.bRemote) {
                file.setFileName(QDir::temp().absolutePath() + "/" + curDT.toString("yyyy.MM.dd_hh.mm.ss") + "_" + m_config.name + ".log");
            } else {
                file.setFileName(newDirOrig + "/qtdsync.log");
            }
            if (file.open(QIODevice::WriteOnly)) {
                QTextStream ts(&file);
                ts << log;
                file.close();
            }
        }

        qApp->processEvents();
#ifdef WIN32
        _flushall();
#endif

        //if (m_config.type == eSynchronize && newDirOrig != lastBackupOrig) {
        //    LOG(true, true, QString("Remove %1").arg(lastBackupOrig));
        //    QtdTools::removeDirectory(QDir(lastBackupOrig));
        //}

        if (pDlg) m_pGlobalProgress->setValue(count);
    }

    foreach (QString fileName, gatheredErrors.keys()) {
        if (openQtd(fileName)) {
            QPair<QDateTime, QString> pair = gatheredErrors[fileName];

            QMessageBox* pBox = new QMessageBox(0L);
            pBox->setWindowTitle(fileName);
            pBox->setIcon(QMessageBox::Warning);
            pBox->setText(tr("The Backup <b>%1</b> returned with an error.").arg(m_config.name));
            pBox->setInformativeText(tr("However, do you you want to mark this backup as valid anyways?"));
            if (pair.second.length() > 0) {
                pBox->setDetailedText(pair.second);
            }
            pBox->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            pBox->setDefaultButton(QMessageBox::Yes);

            bSuccess = (pBox->exec() == QMessageBox::Yes);

            if (!bRestore) {
                m_config.lastBackupTry = pair.first;
                if (bSuccess) {
                    m_config.lastBackup = pair.first;
                }
                m_config.unSaved = true;
                m_config.bCurrentlyBackingUp = false;
                saveQtd();
            }
            if (m_config.fileName == curConfig.fileName) {
                curConfig.lastBackupTry = pair.first;
                if (bSuccess) {
                    curConfig.lastBackup = pair.first;
                }
            }
            delete pBox;
        }
    }


endbackup:
    bGlobalSuccess &= bSuccess;

    foreach (QString dir, createdTempDirs) {
        if (QFileInfo(dir).exists()) {
            QtdTools::removeDirectory(QDir(dir));
        }
    }

    if (pDlg) m_pProgressBar->setValue(m_pProgressBar->maximum());
    if (pDlg) m_pProgressBarFile->setValue(m_pProgressBarFile->maximum());
    if (pDlg) m_pGlobalProgress->setValue(m_pGlobalProgress->maximum());
    if (pDlg) m_pClose->setEnabled(true);
    if (pDlg) m_pClose->setIcon(bSuccess ? QIcon(":/images/ok.png") : QIcon(":/images/cancel.png"));

    delete m_pProcess;
    m_pProcess = 0L;

    if (config.contains("quit") && config["quit"] == "true") {
        quit();
    }

    if (pDlg != pPrevDialog) {
        qApp->processEvents();
        if (pDlg) pDlg->exec();
        if (pDlg) pDlg->deleteLater();
    }

    if (pDlg) {
        m_pDlg = pPrevDialog;
    }

    m_config = curConfig;
    if (!bDryRun) {
        openQtd(m_config.fileName);
    }

    if (pStats) {
        if (bDryRun && (nTotalFileSize*nAllFilesSize == 0)) {
            bGlobalSuccess = false;
        }
        pStats->insert("totalfilesize", nTotalFileSize);
        pStats->insert("allfilessize", nAllFilesSize);
    }
    return bGlobalSuccess;
}

//----------------------------------------------------------------------------
QString QtdSync::evaluateString(QString str, bool bNice)
{
    bool bQtdSync = str.contains("%QtdSync%");
    bool bQtd     = str.contains("%Qtd%");
    bool bPC      = str.contains(m_settings.computerUuid);
    bool bOtherPC = false;
    QString qtdFile("");

    str.replace("%QtdSync%", QString("%1:").arg(qApp->applicationDirPath().at(0)));
    if (QFile(m_config.fileName).exists() || m_config.isNew) {
        str.replace("%Qtd%",     QString("%1:").arg(QFileInfo(m_config.fileName).absoluteFilePath().at(0)));
    }
    str.replace(m_settings.computerUuid, "");

    foreach (QString qtd, m_settings.knownQtd.values()) {
        if (qtd == "") continue;
        if (str.contains(qtd)) {
            qtdFile = QFileInfo(m_settings.knownQtd.key(qtd)).fileName();
            str.replace(qtd, QString("%1:").arg(m_settings.knownQtd.key(qtd).at(0)));
            break;
        }
    }

    if (bNice && str.length() > 2 && str.at(1) == '{') {
        int pos = str.indexOf("}");
        bOtherPC = true;
        str = str.at(0) + str.mid(pos+1);
    }

    if (bNice) {
        if (bQtdSync) {
            str += " " + tr("(bound to the drive where QtdSync is running from)");
        } else if (bQtd) {
            str += " " + tr("(bound to the drive where the backup set file is located)");
        } else if (bPC) {
            str += " " + tr("(bound to this computer)");
        } else if (bOtherPC) {
            str += " " + tr("(bound to another computer)");
        } else if (qtdFile != "") {
            str += " " + tr("(bound to the drive where %1 is located)").arg(qtdFile);
        }
    }
    return str;
}

//----------------------------------------------------------------------------
void QtdSync::initDlg()
{
    if (m_currentDlg == eMain) {

        if (m_geometry.length() > 0) {
            if (m_pDlg) {
                m_pDlg->restoreGeometry(m_geometry);
            }
        }
        m_pW_Remote->hide();
        m_pCB_Name->blockSignals(true);
        m_pCB_Name->clear();
        m_pCB_Name->addItem(m_config.name, QVariant(m_config.fileName));
        foreach (QString qtd, m_settings.knownQtd.keys()) {
            if (QFile(qtd).exists() && qtd.toLower() != m_config.fileName.toLower()) {
                QString name = getQtdName(qtd);
                if (name == "") name = QFileInfo(qtd).fileName();
                m_pCB_Name->addItem(name, QVariant(qtd));
            }
        }
        m_pCB_Name->setCurrentIndex(0);
        m_pCB_Name->blockSignals(false);

        m_pChB_SubFolder->blockSignals(true);
        m_pChB_SubFolder->setChecked(m_config.bCreateSubfolder);
        m_pChB_SubFolder->blockSignals(false);

        m_pGB_RsyncExpert->setVisible(m_settings.showRsyncExpertSettings);
        if (m_settings.showRsyncExpertSettings) {
            m_pLE_RsyncOptions->setText(m_config.globalRsyncOptions);
        }
        m_pCB_BackupType->setCurrentIndex(m_config.type);
        m_pTW_Folders->clear();
        QTreeWidgetItem* pTempItem = new QTreeWidgetItem(m_pTW_Folders, QStringList(tr("Updating.") + tr("Please wait...")));
        pTempItem->setIcon(0, QIcon(":/images/newIcon.png"));
        qApp->processEvents();

        if (m_pDlg) {
            m_pDlg->setEnabled(false);
        }
        
        if (m_config.destination.bRemote) {
            if (m_config.destination.bUseSSH) {
                slot_remoteSSHDestination(false);
            } else {
                slot_remoteDestination();
            }
        } else {
            m_pL_Destination->setText(tr("Destination"));
            m_pW_Remote->hide();
            m_pL_FreeSpace->show();
            m_pGB_BackupInfo_Local->show();
            QString dest = evaluateString(m_config.destination.dest, false);
            m_pLE_Destination->setText(evaluateString(m_config.destination.dest, true));
            if (evaluateString(m_config.destination.dest, true) != "") {
                updateFreeSpaceText(dest);
                //slot_updateFileSizeEstimations();
            } else {
                m_pGB_BackupInfo_Local->hide();
            }
        }

        if (m_config.destination.dest != "") {
            tabWidget->setCurrentIndex(1);
        }

        bool bEnableBackup = false;
        m_pPB_DoBackup->setIcon(QIcon(m_config.destination.bRemote ? (m_config.destination.bUseSSH ? ":/images/icon_green.png":":/images/icon_yellow.png") : ":/images/icon_blue.png"));
        m_pCB_Name->setEnabled(false);

        
        if (m_config.preProcessing.first != "") {
            QTreeWidgetItem* pTWI = new QTreeWidgetItem(m_pTW_Folders, QStringList(tr("Backup Set Preprocessing")));
            pTWI->setHidden(true);
            QString evalFile = evaluateString(m_config.preProcessing.first);
            pTWI->setIcon(0, QIcon(":/images/prebackup.png"));
            pTWI->setData(0, Qt::UserRole | 0x01, -1);
            pTWI->setData(0, Qt::UserRole | 0x02, true);
            pTWI->setData(0, Qt::UserRole | 0x03, 1);

            QTreeWidgetItem* pFile = new QTreeWidgetItem(pTWI, QStringList(evaluateString(m_config.preProcessing.first, true)));
            pFile->setIcon(0, QIcon(QFile(evalFile).exists() ? ":/images/ok.png" : ":/images/cancel.png"));
            pFile->setData(0, Qt::UserRole | 0x01, -1);
            pFile->setData(0, Qt::UserRole | 0x02, true);

            if (m_config.preProcessing.second != "") {
                pFile = new QTreeWidgetItem(pTWI, QStringList(tr("with the arguments")));
                QFont font = pFile->font(0);
                font.setBold(true);
                pFile->setFont(0, font);
                pFile->setData(0, Qt::UserRole | 0x01, -1);
                pFile->setData(0, Qt::UserRole | 0x02, true);

                pTWI = new QTreeWidgetItem(pFile, QStringList(m_config.preProcessing.second));
                pTWI->setData(0, Qt::UserRole | 0x01, -1);
                pTWI->setData(0, Qt::UserRole | 0x02, true);
            }
        }

        for (int i = 0; i < m_config.folders.count(); i++) {
            QTreeWidgetItem* pTWI = new QTreeWidgetItem(m_pTW_Folders, QStringList(m_config.folders.at(i).name));
            pTWI->setHidden(true);
            QString evalFolder = evaluateString(m_config.folders.at(i).folder);
            bEnableBackup |= QDir(evalFolder).exists();
            pTWI->setIcon(0,QIcon(m_config.destination.bRemote ? (m_config.destination.bUseSSH ? ":/images/icon_green.png":":/images/icon_yellow.png") : ":/images/icon_blue.png"));
            pTWI->setData(0, Qt::UserRole | 0x01, -1);
            pTWI->setData(0, Qt::UserRole | 0x02, true);
            pTWI->setData(0, Qt::UserRole | 0x03, 0);
            QTreeWidgetItem* pFolder = new QTreeWidgetItem(pTWI, QStringList(evaluateString(m_config.folders.at(i).folder, true)));
            pFolder->setData(0, Qt::UserRole, evalFolder);
            pFolder->setData(0, Qt::UserRole | 0x01, i);
            pFolder->setData(0, Qt::UserRole | 0x02, false);
            fillTreeElement(pFolder, false, 1);

            pFolder->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            pTWI->setExpanded(true);
            if (m_config.folders.at(i).exclude != "") {
                pFolder = new QTreeWidgetItem(pTWI, QStringList(tr("except")));
                QFont font = pFolder->font(0);
                font.setBold(true);
                pFolder->setFont(0, font);
                pFolder->setData(0, Qt::UserRole | 0x01, -1);
                pFolder->setData(0, Qt::UserRole | 0x02, true);
                QStringList excludes = m_config.folders.at(i).exclude.split(";");
                foreach (QString excl, excludes) {
                    pTWI = new QTreeWidgetItem(pFolder, QStringList(excl));
                    pTWI->setData(0, Qt::UserRole | 0x01, -1);
                    pTWI->setData(0, Qt::UserRole | 0x02, true);
                }
                pFolder->setExpanded(true);
            }

        }

        if (m_config.postProcessing.first != "") {
            QTreeWidgetItem* pTWI = new QTreeWidgetItem(m_pTW_Folders, QStringList(tr("Backup Set Postprocessing")));
            pTWI->setHidden(true);
            QString evalFile = evaluateString(m_config.postProcessing.first);
            pTWI->setIcon(0, QIcon(":/images/postbackup.png"));
            pTWI->setData(0, Qt::UserRole | 0x01, -1);
            pTWI->setData(0, Qt::UserRole | 0x02, true);
            pTWI->setData(0, Qt::UserRole | 0x03, 2);

            QTreeWidgetItem* pFile = new QTreeWidgetItem(pTWI, QStringList(evaluateString(m_config.postProcessing.first, true)));
            pFile->setIcon(0, QIcon(QFile(evalFile).exists() ? ":/images/ok.png" : ":/images/cancel.png"));
            pFile->setData(0, Qt::UserRole | 0x01, -1);
            pFile->setData(0, Qt::UserRole | 0x02, true);

            if (m_config.postProcessing.second != "") {
                pFile = new QTreeWidgetItem(pTWI, QStringList(tr("with the arguments")));
                QFont font = pFile->font(0);
                font.setBold(true);
                pFile->setFont(0, font);
                pFile->setData(0, Qt::UserRole | 0x01, -1);
                pFile->setData(0, Qt::UserRole | 0x02, true);

                pTWI = new QTreeWidgetItem(pFile, QStringList(m_config.postProcessing.second));
                pTWI->setData(0, Qt::UserRole | 0x01, -1);
                pTWI->setData(0, Qt::UserRole | 0x02, true);
            }
        }


        slot_subfolderStateChanged(0, false);

        if (m_pDlg) {
            m_pDlg->setEnabled(true);
        }

        m_pCB_Name->setEnabled(true);
        m_pPB_DoBackup->setEnabled(bEnableBackup);

        delete pTempItem;

        for (int i = 0; i < m_pTW_Folders->topLevelItemCount(); i++) {
            m_pTW_Folders->topLevelItem(i)->setHidden(false);
        }

    } else if (m_currentDlg == eServer) {
        if (m_pDlg) {
            m_pDlg->setEnabled(false);
        }
        updateServerUsers();
        updateServerDirectories();
        updateServerStatus();
        if (m_pDlg) {
            m_pDlg->setEnabled(true);
        }
    }
}

//----------------------------------------------------------------------------
void QtdSync::clearTreeElement(QTreeWidgetItem* pItem)
{
    if (!pItem) return;
    // clear current tree
    QList<QTreeWidgetItem*> pChildren = pItem->takeChildren();
    for (int i = 0; i < pChildren.count(); i++) {
        clearTreeElement(pChildren.at(i));
        delete pChildren.at(i);
    }
    pItem->setData(0, Qt::UserRole | 0x02, false);
}

//----------------------------------------------------------------------------
void QtdSync::fillTreeElement(QTreeWidgetItem* curIt, bool bFoldersOnly, int nRecursiveLevels)
{
    if (!curIt || qApp->closingDown()) return;

    m_nFillingTreeDepth++;

    QString dir = curIt->data(0, Qt::UserRole).toString();
    QString path("");
    QStringList excludes, filesExcluded;
    
    if (m_currentDlg == eMain) {
        Folder fold = m_config.folders.at(curIt->data(0, Qt::UserRole | 0x01).toInt());
        path = fold.folder;
        excludes = fold.exclude.split(";");
        filesExcluded = fold.filesExcluded;
    } else {
        VirtualDirConfig dir = m_serverSettings.dirs.at(curIt->data(0, Qt::UserRole | 0x01).toInt());
        path = dir.path;
    }

    QFileIconProvider iconProvider;
    
    clearTreeElement(curIt);

    if (QDir(dir).exists() || QFile(dir).exists()) {
        curIt->setIcon(0, iconProvider.icon(QFileInfo(dir)));
        if (!QDir(dir).exists()) {
            dir = "";
        }
    } else {
        curIt->setIcon(0, QIcon(":/images/delete.png"));
        curIt->setToolTip(0, tr("Folder not found!"));
        return;
    }
    if (dir == "") {
        return;
    }

    QFileInfoList elms;
    int step = 0;
    
    for (step = 0; step < 2; step++) {
        if (step == 0) {
            elms = QDir(dir).entryInfoList(QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        } else {
            elms = QDir(dir).entryInfoList(QDir::Files | QDir::Hidden | QDir::System, QDir::Name | QDir::IgnoreCase);
        }

        foreach(QFileInfo elm, elms) {
            QTreeWidgetItem* pNew = 0L;
            QString name = elm.fileName();
            name.replace("\\", "/");
            QString relativePath = "/" + QDir(evaluateString(path)).relativeFilePath(elm.absoluteFilePath());
            bool exclude = false;
            foreach(QString exc, excludes) {
                QRegExp regExp(QString("^(%1)$").arg(exc).replace("*", ".*").replace("?", "."));
                if (regExp.indexIn(elm.fileName()) != -1) {
                    exclude = true;
                }
            }
            if (!exclude && (!bFoldersOnly || (bFoldersOnly && elm.isDir()))) {
                pNew = new QTreeWidgetItem(curIt, QStringList(name));
                pNew->setIcon(0, iconProvider.icon(elm));
                pNew->setData(0, Qt::UserRole | 0x01, curIt->data(0, Qt::UserRole | 0x01).toInt());
                pNew->setData(0, Qt::UserRole, QString(elm.absoluteFilePath()));
                pNew->setData(0, Qt::UserRole | 0x02, false);

                if (m_currentDlg == eMain) {
                    pNew->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
                    pNew->setCheckState(0, filesExcluded.contains(relativePath) ? Qt::Unchecked : Qt::Checked);
                }
            }

            bool bDoIt = m_currentDlg == eMain ? !filesExcluded.contains(relativePath) : true;
            if (pNew && elm.isDir() && bDoIt) {
                if (nRecursiveLevels > 0) {
                    fillTreeElement(pNew, bFoldersOnly, nRecursiveLevels - 1);
                }
            }
            qApp->processEvents();
        }
    }
    curIt->setData(0, Qt::UserRole | 0x02, nRecursiveLevels > 0);

    m_nFillingTreeDepth--;
}

//----------------------------------------------------------------------------
// File access functions
//----------------------------------------------------------------------------
bool QtdSync::readQtd(QString content, QString filename)
{
    QDomDocument curDom;
    clearQtd();

    if (filename != "") {
        m_config.fileName = filename;
    }

    if (!curDom.setContent(content)) {
        return false;
    }

    QDomNode root = curDom.firstChildElement("QtdSync");
    if (root.isNull()) {
        return false;
    }

    QDomNamedNodeMap rAtts = root.attributes();
    m_config.name = QtdTools::codecSaveString(rAtts.namedItem("name").nodeValue());
    m_config.lastBackup = QDateTime::fromString(root.toElement().attribute("lastBackup", ""), Qt::ISODate);
    m_config.lastBackupTry = QDateTime::fromString(root.toElement().attribute("lastBackupTry", ""), Qt::ISODate);

    if (rAtts.contains("createSubfolder")) {
        m_config.bCreateSubfolder = (rAtts.namedItem("createSubfolder").nodeValue() != "false");
    }
    int version = 1;
    if (rAtts.contains("version")) {
        version = rAtts.namedItem("version").nodeValue().toInt();
    }
    if (rAtts.contains("currentlyDone")) {
        m_config.bCurrentlyBackingUp = true;
    }

    if (rAtts.contains("type")) {
        if (rAtts.namedItem("type").nodeValue() == "synchronize") {
            m_config.type = eSynchronize;
        } else if (rAtts.namedItem("type").nodeValue() == "differential") {
            m_config.type = eDifferential;
        }
    } else {
        m_config.type = eDifferential;
    }

    if (rAtts.contains("uuid")) {
        m_config.uuid = rAtts.namedItem("uuid").nodeValue();
    }

    m_config.folders.clear();

    QDomElement nNode = root.firstChildElement("destination");
    m_config.destination.dest = QtdTools::codecSaveString(nNode.text());
    if (nNode.hasAttribute("path")) {
        m_config.destination.bRemote = true;
        m_config.destination.path = QtdTools::codecSaveString(nNode.attribute("path"));
        if (nNode.hasAttribute("user")) {
            m_config.destination.bUserAuth = true;
            m_config.destination.user = nNode.attribute("user");
            m_config.destination.bUseSSH = m_bSSHAvailable && nNode.hasAttribute("useSSH");
            if (m_config.destination.bUseSSH) {
                m_config.destination.bUsePassword = m_bSSHAvailable && nNode.hasAttribute("usePassword");
            }
            if (nNode.hasAttribute("password")) {
                m_config.destination.password = nNode.attribute("password");
                if (version >= 4) {
                    QByteArray passStr = QByteArray::fromBase64(m_config.destination.password.toAscii());
                    QtdCrypt::decrypt(passStr, QTD_PASS_HASH);
                    m_config.destination.password = QString(passStr);
                } else if (version == 3) {
                    m_config.destination.password = "";
                }
                m_config.destination.bStorePassWd = m_config.destination.password != "";
            }
        }
    }

    nNode = root.firstChildElement("globalRsyncOptions");
    if (!nNode.isNull()) {
        m_config.globalRsyncOptions = nNode.text();
    }

    nNode = root.firstChildElement("preProcessing");
    if (!nNode.isNull()) {
        m_config.preProcessing.first = QtdTools::codecSaveString(nNode.text());
        m_config.preProcessing.second = QtdTools::codecSaveString(nNode.attribute("arguments", ""));
    }

    nNode = root.firstChildElement("postProcessing");
    if (!nNode.isNull()) {
        m_config.postProcessing.first = QtdTools::codecSaveString(nNode.text());
        m_config.postProcessing.second = QtdTools::codecSaveString(nNode.attribute("arguments", ""));
    }

    nNode = root.firstChildElement("schedule");
    if (!nNode.isNull()) {
        m_config.schedule.bOnPlugin     = nNode.attribute("onPlugin", "false") == "true";
        m_config.schedule.bOnShutdown   = nNode.attribute("onShutdown", "false") == "true";
        m_config.schedule.bOnStartup    = nNode.attribute("onStartup", "false") == "true";
        m_config.schedule.bSilent       = nNode.attribute("silent", "false") == "true";
        m_config.schedule.bMarkFailedAsValid = nNode.attribute("markFailedAsValid", "false") == "true";
        m_config.schedule.bOnTime       = nNode.attribute("onTime", "false") == "true";
        m_config.scheduledPrio          = nNode.attribute("processPriority", "2").toInt();

        QDomElement condNode = nNode.firstChildElement("condition");
        if (!condNode.isNull()) {
            m_config.schedule.conditionType = condNode.attribute("type", "day");
            m_config.schedule.nConditionCount = condNode.text().toInt();
        }

        QDomElement retryNode = nNode.firstChildElement("retryOnFailure");
        if (!retryNode.isNull()) {
            m_config.schedule.bRetry    = retryNode.attribute("doIt", "true") == "true";
            m_config.schedule.retryType = retryNode.attribute("type", "hour");
            m_config.schedule.nRetryNumber = retryNode.text().toInt();
        } else {
            m_config.schedule.bRetry = false;
            m_config.schedule.retryType = retryNode.attribute("type", "hour");
            m_config.schedule.nRetryNumber = 1;
        }

        condNode = nNode.firstChildElement("start");
        if (!condNode.isNull()) {
            m_config.schedule.startTime = QDateTime::fromString(condNode.text(), Qt::ISODate);
        }

        condNode = nNode.firstChildElement("every");
        if (!condNode.isNull()) {
            m_config.schedule.everyType = condNode.attribute("type", "day");
            m_config.schedule.nEveryCount = condNode.text().toInt();
        }

    }

    m_settings.schedule[m_config.uuid] = m_config.schedule;
    
    nNode = root.firstChildElement("mailReport");
    if (!nNode.isNull()) {
        m_config.mail = QtdMail();
        m_config.mail.fromXml(nNode);
        m_config.sendReport = nNode.attribute("doIt", "false") == "true";
        m_config.sendReportOnErrorOnly = nNode.attribute("onErrorOnly", "false") == "true";
    }

    QDomNodeList nLst = root.toElement().elementsByTagName("folder");
    for (int i = 0; i < nLst.count(); i++) {
        QDomNode cNode = nLst.at(i);
        Folder fold;

        QDomNamedNodeMap cAtts = cNode.attributes();
        if (cAtts.contains("name"))     fold.name       = QtdTools::codecSaveString(cAtts.namedItem("name").nodeValue());
        if (fold.name == "") {
            fold.name = tr("Folder %1").arg(i);
        }

        if (cAtts.contains("createSubfolder")) {
            fold.bCreateSubFolder = (cAtts.namedItem("createSubfolder").nodeValue() != "false");
        } else {
            fold.bCreateSubFolder = true;
        }

        if (version < 2) {
            if (cAtts.contains("exclude"))  fold.exclude    = QtdTools::codecSaveString(cAtts.namedItem("exclude").nodeValue().split(";", QString::SkipEmptyParts).join(";"));
            fold.folder = QtdTools::codecSaveString(cNode.toElement().text());
        } else {
            fold.exclude = QtdTools::codecSaveString(cNode.firstChildElement("exclude").text().split(";", QString::SkipEmptyParts).join(";"));
            fold.folder  = QtdTools::codecSaveString(cNode.firstChildElement("dir").text());
            if (!cNode.firstChildElement("rsync").isNull()) {
                fold.rsyncExpert = cNode.firstChildElement("rsync").text();
            } else {
                fold.rsyncExpert = "use-global-settings";
            }
            QDomNodeList nLst2 = cNode.toElement().elementsByTagName("file");
            for (int j = 0; j < nLst2.count(); j++) {
                fold.filesExcluded << QtdTools::codecSaveString(nLst2.at(j).toElement().text());
            }
        }

        if (fold.folder != "") {
            m_config.folders << fold;
        }
    }

    m_curQtd = writeQtd();

    m_config.unSaved  = false;
    m_config.isNew    = false;
    return true;
}

//----------------------------------------------------------------------------
QString QtdSync::writeQtd(bool bRealSaving)
{
    bool timerRunning = false;
    if (m_timer) {
        if (timerRunning = m_timer->isActive()) {
            m_timer->stop();
        }
    }

    QDomDocument doc("QtdSync");
    doc.appendChild(doc.createProcessingInstruction("xml",QString("version=\"1.0\" encoding=\"%1\"" ).arg("utf-8")));

    QDomElement root = doc.createElement("QtdSync");
    root.setAttribute("name", QtdTools::codecSaveString(m_config.name));
    root.setAttribute("version", QString("%1").arg(QTD_VERSION));
    root.setAttribute("uuid", m_config.uuid);
    if (!m_config.bCreateSubfolder) {
        root.setAttribute("createSubfolder", "false");
    }
    if (m_config.bCurrentlyBackingUp) {
        root.setAttribute("currentlyDone", "true");
    }
    if (m_config.lastBackup.isValid()) {
        root.setAttribute("lastBackup", m_config.lastBackup.toString(Qt::ISODate));
    }

    if (m_config.lastBackupTry.isValid()) {
        root.setAttribute("lastBackupTry", m_config.lastBackupTry.toString(Qt::ISODate));
    }

    switch (m_config.type) {
        case eSynchronize:
            root.setAttribute("type", "synchronize");
            break;
        case eDifferential:
            root.setAttribute("type", "differential");
            break;
        default:
            break;
    }


    QDomElement nNode = doc.createElement("schedule");
    nNode.setAttribute("onPlugin",      m_config.schedule.bOnPlugin ? "true":"false");
    nNode.setAttribute("onShutdown",    m_config.schedule.bOnShutdown ? "true":"false");
    nNode.setAttribute("onStartup",     m_config.schedule.bOnStartup ? "true":"false");
    nNode.setAttribute("silent",        m_config.schedule.bSilent ? "true":"false");
    nNode.setAttribute("markFailedAsValid", m_config.schedule.bMarkFailedAsValid ? "true":"false");
    nNode.setAttribute("onTime",        m_config.schedule.bOnTime ? "true":"false");
    nNode.setAttribute("processPriority",m_config.scheduledPrio);

    QDomElement condNode = doc.createElement("condition");
    condNode.setAttribute("type", m_config.schedule.conditionType);
    condNode.appendChild(doc.createTextNode(QString("%1").arg(m_config.schedule.nConditionCount)));
    nNode.appendChild(condNode);

    condNode = doc.createElement("retryOnFailure");
    condNode.setAttribute("doIt", m_config.schedule.bRetry ? "true" : "false");
    condNode.setAttribute("type", m_config.schedule.retryType);
    condNode.appendChild(doc.createTextNode(QString("%1").arg(m_config.schedule.nRetryNumber)));
    nNode.appendChild(condNode);

    condNode = doc.createElement("start");
    condNode.appendChild(doc.createTextNode(m_config.schedule.startTime.toString(Qt::ISODate)));
    nNode.appendChild(condNode);

    condNode = doc.createElement("lastTry");
    condNode.appendChild(doc.createTextNode(m_config.schedule.lastTry.toString(Qt::ISODate)));
    nNode.appendChild(condNode);

    condNode = doc.createElement("every");
    condNode.setAttribute("type", m_config.schedule.everyType);
    condNode.appendChild(doc.createTextNode(QString("%1").arg(m_config.schedule.nEveryCount)));
    nNode.appendChild(condNode);
    root.appendChild(nNode);

    QDomElement node = doc.createElement("destination");
	if (!m_config.destination.bRemote) {
		QString evalFolder = evaluateString(m_config.destination.dest);
		if (bRealSaving && (!QDir(evalFolder).exists() && !(evalFolder.length() > 2 && evalFolder.at(1) == '{'))) {
			localDestinationMissing();
		}
	}
    node.appendChild(doc.createTextNode(QtdTools::codecSaveString(m_config.destination.dest)));

    if (m_config.destination.bRemote) {
        node.setAttribute("path", QtdTools::codecSaveString(m_config.destination.path));
        if (m_config.destination.bUserAuth) {
            node.setAttribute("user", m_config.destination.user);
            if (m_config.destination.bUseSSH) {
                node.setAttribute("useSSH", "true");
                if (m_config.destination.bUsePassword) {
                    node.setAttribute("usePassword", "true");
                }
            }
            if (m_config.destination.bStorePassWd && m_config.destination.password != "") {
                QByteArray passStr = m_config.destination.password.toAscii();
                QtdCrypt::encrypt(passStr, QTD_PASS_HASH);
                passStr = passStr.toBase64();
                node.setAttribute("password", QString(passStr));
            }
            
        }
    }
    root.appendChild(node);

    node = doc.createElement("globalRsyncOptions");
    node.appendChild(doc.createTextNode(m_config.globalRsyncOptions));
    root.appendChild(node);

    if (m_config.preProcessing.first != "") {
        node = doc.createElement("preProcessing");
        if (m_config.preProcessing.second != "") {
            node.setAttribute("arguments", QtdTools::codecSaveString(m_config.preProcessing.second));
        }
        node.appendChild(doc.createTextNode(QtdTools::codecSaveString(m_config.preProcessing.first)));
        root.appendChild(node);
    }

    if (m_config.postProcessing.first != "") {
        node = doc.createElement("postProcessing");
        if (m_config.postProcessing.second != "") {
            node.setAttribute("arguments", QtdTools::codecSaveString(m_config.postProcessing.second));
        }
        node.appendChild(doc.createTextNode(QtdTools::codecSaveString(m_config.postProcessing.first)));
        root.appendChild(node);
    }

    // email notification
    node = doc.createElement("mailReport");
    if (m_config.mail.toXml(node)) {
        node.setAttribute("doIt", m_config.sendReport ? "true" : "false");
        if (m_config.sendReportOnErrorOnly) {
            node.setAttribute("onErrorOnly", "true");
        }
        root.appendChild(node);
    }

    // folders
    QList<int> foldersToBeRemoved;
    bool bAskForMissingFolder = bRealSaving;
    for (int i = 0; i < m_config.folders.count(); i++) {
        Folder fold = m_config.folders.at(i);

        QDomElement cNode = doc.createElement("folder");
        cNode.setAttribute("name",      QtdTools::codecSaveString(fold.name));

        if (!fold.bCreateSubFolder) {
            cNode.setAttribute("createSubfolder", "false");
        }

        QDomElement c2Node = doc.createElement("exclude");
        c2Node.appendChild(doc.createTextNode(QtdTools::codecSaveString(fold.exclude)));
        cNode.appendChild(c2Node);

        c2Node = doc.createElement("rsync");
        c2Node.appendChild(doc.createTextNode(fold.rsyncExpert));
        cNode.appendChild(c2Node);

        c2Node = doc.createElement("dir");
        QString evalFolder = evaluateString(fold.folder);
        if (bAskForMissingFolder && (!QDir(evalFolder).exists() && !(evalFolder.length() > 2 && evalFolder.at(1) == '{'))) {
            m_pUpdateDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);
            Ui::QtdSyncFolderBinding bUi;

            bool bBoundToQtdSync = fold.folder.contains("%QtdSync%");
            bool bBoundToQtd     = fold.folder.contains("%Qtd%");
            bool bBoundNoWhere   = !bBoundToQtdSync && !bBoundToQtd;

            bUi.SetupUi(m_pUpdateDlg);
            bUi.m_pCaption->setText(bBoundNoWhere ? tr("Folder not found") : tr("Error on folder binding"));
            bUi.m_pBindToQtdSync->setVisible(false);
            bUi.m_pAbsoluteBind->setVisible(false);
            bUi.m_pBindToQtd->setVisible(true);
            bUi.m_pBindToQtd->setText(tr("Ignore this error"));
            bUi.m_pBindToQtd->setIcon(QIcon(":/images/delete.png"));
            bUi.m_pAbsolute->setText(tr("Relocated folder"));
            bUi.m_pBottomLine->hide();
            bUi.m_pBottomLine2->hide();
            bUi.m_pBindToSelectedQtd->hide();
            bUi.m_pCB_QtdFiles->hide();
            bUi.m_pAbsoluteBind->hide();

            QString driveLetter = bBoundToQtd ? QFileInfo(m_config.fileName).absoluteFilePath().mid(0,1) : qApp->applicationDirPath().mid(0,1);
            if (!bBoundNoWhere) {
                bUi.m_pInfo->setText(tr("You have bound the folder<br><center><b>%1</b></center><br>to the drive where %2 is located.<br>But this folder does not exist on the drive <b>%3:</b>.").arg(evaluateString(fold.folder).mid(2)).arg(bBoundToQtdSync ? "QtdSync" : tr("the backup set file")).arg(driveLetter));
            } else {
                bUi.m_pInfo->setText(tr("The folder<br><center><b>%1</b></center><br>does not exist.").arg(evaluateString(fold.folder)));
            }

            connect(bUi.m_pBindToQtd,       SIGNAL(clicked()), m_pUpdateDlg, SLOT(accept()));

            qApp->processEvents();
            m_pUpdateDlg->adjustSize();

            int ret = m_pUpdateDlg->exec();
            bAskForMissingFolder = !bUi.m_pChB_DoNotAskAgain->isChecked();
            delete m_pUpdateDlg;
            m_pUpdateDlg = 0L;

            if (ret == QDialog::Rejected) {
                QString folder = browseForFolder();
                if (folder == "") {
                    i--;
                    continue;
                } else {
                    fold.folder = folder;
                    m_config.folders.replace(i, fold);
                    m_bRebuildTree |= true;
                }
            }/* else {
                foldersToBeRemoved << i;
                continue;
            }*/
        }
        c2Node.appendChild(doc.createTextNode(QtdTools::codecSaveString(fold.folder)));
        cNode.appendChild(c2Node);

        if (fold.filesExcluded.count() > 0) {
            c2Node = doc.createElement("excludedFiles");
            foreach (QString file, fold.filesExcluded) {
                QDomElement fNode = doc.createElement("file");
                fNode.appendChild(doc.createTextNode(QtdTools::codecSaveString(file)));
                c2Node.appendChild(fNode);
            }
            cNode.appendChild(c2Node);
        }

        root.appendChild(cNode);
    }

    //QList<Folder> newFolders;
    //newFolders << m_config.folders;
    //m_config.folders.clear();

    //for (int i = 0; i < newFolders.count(); i++) {
    //    if (!foldersToBeRemoved.contains(i)) m_config.folders << newFolders.at(i);
    //}

    //m_bRebuildTree |= foldersToBeRemoved.count() > 0;

    doc.appendChild(root);
    if (m_timer) {
        if (timerRunning) m_timer->start(500);
    }
    return doc.toString(4);
}



//----------------------------------------------------------------------------
void QtdSync::clearQtd(bool bCheckName)
{
    int nCount = 1;
    while (true) {
        bool bFound = false;
        m_config.name         = tr("New backup set") + (nCount > 1 ? QString(" %1").arg(nCount) : "");
        if (bCheckName) {
            foreach (QString qtd, m_settings.knownQtd.keys()) {
                if (QFile(qtd).exists()) {
                    QString name = getQtdName(qtd);
                    if (name == m_config.name) {
                        bFound = true;
                        break;
                    }
                }
            }

            if (bFound) {
                nCount++;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    m_config.bCreateSubfolder           = true;
    m_config.fileName     = m_config.name + ".qtd";
    m_config.destination.bRemote        = false;
    m_config.destination.bUserAuth      = false;
    m_config.destination.dest           = "";
    m_config.destination.user           = "";
    m_config.destination.password       = "";
    m_config.destination.bStorePassWd   = false;
    m_config.destination.bUseSSH        = false;
    m_config.destination.bUsePassword   = false;

    m_config.folders.clear();
    m_config.globalRsyncOptions         = QTD_DEF_RSYNC_OPTIONS;
    m_config.unSaved                    = false;
    m_config.isNew                      = true;
    m_config.type                       = eSynchronize;
    m_config.lastBackup                 = QDateTime::fromString("");
    m_config.lastBackupTry              = QDateTime::fromString("");
    m_config.uuid                       = QUuid::createUuid().toString();
    m_config.bCurrentlyBackingUp        = false;

    m_config.schedule.bOnPlugin         = false;
    m_config.schedule.bOnShutdown       = false;
    m_config.schedule.bOnStartup        = false;
    m_config.schedule.bSilent           = false;
    m_config.schedule.bMarkFailedAsValid = false;
    m_config.schedule.bOnTime           = false;
    m_config.schedule.startTime         = QDateTime::currentDateTime();
    m_config.schedule.bRetry            = false;
    m_config.schedule.nConditionCount   = 1;
    m_config.schedule.nEveryCount       = 1;
    m_config.schedule.everyType         = "day";
    m_config.schedule.conditionType     = "day";
    m_config.schedule.retryType         = "minute";
    m_config.schedule.nRetryNumber      = 1;

    m_config.preProcessing.first        = "";
    m_config.postProcessing.first       = "";

    m_config.mail                       = QtdMail();
    m_config.sendReport                 = false;
    m_config.sendReportOnErrorOnly      = false;
    m_config.scheduledPrio              = (int)QtdTools::eProcessPriority_Normal;

    m_curQtd = writeQtd();
}

//----------------------------------------------------------------------------
bool QtdSync::saveQtd()
{
	if (m_pDlg && m_currentDlg == eMain) {
		if (!m_config.destination.bRemote) {
			if (m_pLE_Destination->text() != evaluateString(m_config.destination.dest, true)) {
				m_config.destination.dest = evaluateString(m_pLE_Destination->text());
				slot_browseDestination(m_config.destination.dest);
				m_config.unSaved = qtdHasChanged();
			}
		} else {
			slot_destEdited();
			m_config.unSaved = qtdHasChanged();
		}
    }

    if (m_config.unSaved && m_config.isNew) {
        if (m_config.name != "") {
            int nCount = 1;
            while (true) {
                m_config.fileName = QFileInfo(m_config.fileName).absolutePath() + "/" + m_config.name + (nCount > 1 ? QString(" (%1)").arg(nCount) :"") + ".qtd";
                if (!QFile(m_config.fileName).exists()) {
                    break;
                }
                nCount++;
            }
        }
    }

    QString fileName = m_config.fileName;
    if (!m_config.unSaved) return true;
    if (m_config.isNew) {
        fileName = QFileDialog::getSaveFileName(m_pDlg, tr("Save %1 Backup set file").arg(m_productName), fileName, tr("Qtd-File (*.qtd)"));
        if (fileName == "") {
            return false;
        }
    }

    if (m_currentDlg == eMain) {
        m_pCB_Name->setItemData(m_pCB_Name->currentIndex(), QVariant(fileName));
    }

    QFile file(fileName);
    if (m_config.name == "") {
        m_config.name = QFileInfo(fileName).baseName();
    }
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(m_pDlg, tr("Error opening file"), tr("File <b>%1</b> could not be opened for writing!").arg(fileName));
        return false;
    }

    QString oldConfigName = m_config.fileName;
    m_config.fileName = QFileInfo(fileName).absoluteFilePath();
    m_config.isNew    = false;

    m_curQtd = writeQtd(true);
    if (m_curQtd == "") {
        m_config.fileName = oldConfigName;
        return false;
    }
    QTextStream ts(&file);
    ts << m_curQtd.toUtf8();
    file.close();

    m_settings.lastModified[m_config.uuid] = QDateTime::currentDateTime();

    m_config.fileName = fileName;
    m_config.unSaved  = false;

    addToKnownQtd(m_config.fileName);
    return true;
}


//----------------------------------------------------------------------------
bool QtdSync::qtdHasChanged()
{
    return m_curQtd != writeQtd();
}

//----------------------------------------------------------------------------
QPair<QStringList, QStringList> QtdSync::validateKnownQtd()
{
    bool changed = false;
    QStringList removed;
    QStringList added;

    foreach (QString qtd, m_settings.knownQtd.keys()) {
        if (!QFile(qtd).exists()) {
            removed << qtd;
            if (m_settings.monitor.bDriveLetters) {
                QFileInfoList drives = QDir::drives();
                foreach (QFileInfo drive, drives) {
                    char driveLetter = drive.absolutePath().toUpper().at(0).toAscii();
                    if (m_settings.monitor.driveTypes.contains(QtdTools::getDriveType(driveLetter))) {
                        QString fileName = QString("%1%2").arg(drive.absolutePath()).arg(qtd.mid(3));
                        if (QFile(fileName).exists()) {
                            if (addToKnownQtd(fileName)) {
                                changed = true;
                                if (qtd != fileName) removed << qtd;
                                added << fileName;
                            }
                        }
                    }
                }
            }
        } else {
            QString uuid = getQtdUuid(qtd);
            if (uuid != "" && m_settings.knownQtd[qtd] != uuid) {
                changed |= true;
                m_settings.knownQtd[qtd] = uuid;
                added << qtd;
            }
        }
    }
    if (changed) {
        saveConfig();
    }

    return qMakePair(removed, added);
}

//----------------------------------------------------------------------------
bool QtdSync::addToKnownQtd(QString fileName)
{
    QString uuid = getQtdUuid(fileName);
    if (uuid == "") {
        uuid = QUuid::createUuid().toString();
        if (setQtdUuid(fileName, uuid)) {
            m_settings.knownQtd[fileName] = uuid;
        } else {
            return false;
        }
    } else {
        if (m_settings.knownQtd.values().contains(uuid)) {
            if (m_settings.knownQtd.key(uuid) == fileName) {
                return false;
            }
            m_settings.knownQtd.remove(m_settings.knownQtd.key(uuid));
        }
        if (m_settings.knownQtd.contains(fileName)) {
            if (m_settings.knownQtd[fileName] == uuid) {
                return false;
            }
        }
        m_settings.knownQtd[fileName] = uuid;
    }
    return true;
}

//----------------------------------------------------------------------------
QMap<QString, QVariant> QtdSync::doDryRun()
{
    QMap<QString, QString> config;
    QMap<QString, QVariant> stats;
    config["dryrun"] = "true";

    if (!doBackup(QStringList(), config, &stats)) {
        stats.insert("failed", "true");
    }

    return stats;
}

//----------------------------------------------------------------------------
void QtdSync::slot_updateFileSizeEstimations()
{
    if (!m_pDlg || m_currentDlg != eMain) {
        return;
    }

    m_pDlg->setEnabled(false);

#ifndef QT_NO_CURSOR
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
#endif

    QMap<QString, QVariant> stats = doDryRun();
    if (stats.contains("failed") && stats["allfilessize"].toULongLong() == 0) {
        m_pL_TotalTransferredSize->setText(tr("Unknown"));
        m_pL_TotalFileSize->setText(tr("Unknown"));
    } else for (int step = 0; step < 2; step++) {
        QtdTools::FreeSpaceType eType;
        double dFreeSpace = 0.0;
        
        if (step == 0) {
            dFreeSpace = (double)(stats["totalfilesize"].toULongLong());
        } else {
            dFreeSpace = (double)(stats["allfilessize"].toULongLong());
        }
        QString text = "%1 kB";
        if (dFreeSpace > 2048) {
            dFreeSpace /= 1024.0;
            text = "%1 MB";
            if (dFreeSpace > 2048) {
                dFreeSpace /= 1024.0;
                text = "%1 GB";
                if (dFreeSpace > 2048) {
                    dFreeSpace /= 1024.0;
                    text = "%1 TB";
                }
            }
        }
        
        if (step == 0) {
            m_pL_TotalTransferredSize->setText(text.arg(dFreeSpace, 0, 'f', 2));
        } else {
            m_pL_TotalFileSize->setText(text.arg(dFreeSpace, 0, 'f', 2));
        }
    }

#ifndef QT_NO_CURSOR
    QApplication::restoreOverrideCursor();
#endif
    m_pDlg->setEnabled(true);

}

//----------------------------------------------------------------------------
void QtdSync::updateFreeSpaceText(QString dir)
{
    QString text = tr("%1 kB free");
    double dFreeSpace = 0.0;
    if (dir != "" && QDir(dir).exists()) {
        QtdTools::FreeSpaceType eType;
        dFreeSpace = QtdTools::freeSpace(dir, eType);
        switch (eType) {
            case QtdTools::eFreeSpace_MB:
                text = tr("%1 MB free");
                break;
            case QtdTools::eFreeSpace_GB:
                text = tr("%1 GB free");
                break;
            case QtdTools::eFreeSpace_TB:
                text = tr("%1 TB free");
                break;
            default:
                break;
        }
    } else {
        text = "";
    }

    m_pL_FreeSpace->setText(text.arg(dFreeSpace, 0, 'f', 2));
}

//----------------------------------------------------------------------------
void QtdSync::checkFstab()
{
#ifdef WIN32
    bool writeIt = false;
    if (!QFile(qApp->applicationDirPath() + "/etc/fstab").exists()) {
        writeIt = true;
        // create fstab
        if (!QDir(qApp->applicationDirPath()).exists("etc")) {
            writeIt = QDir(qApp->applicationDirPath()).mkdir("etc");
        }

        if (writeIt) {
            QString fstab = QtdTools::readFile(":/fstab");
            if (fstab != "") {
                QtdTools::writeFile(qApp->applicationDirPath() + "/etc/fstab", fstab, false);
            }
        }
    }
#endif
}


//----------------------------------------------------------------------------
bool QtdSync::isMonitorRunning()
{
    saveConfig();
    loadConfig();
#ifdef WIN32
    QMap<unsigned long, QString> procs = QtdTools::getAllProcesses(QFileInfo(qApp->applicationFilePath()).fileName());
    return procs.contains(m_settings.monitor.nProcessId);
#else
    return QDir(QString("/proc/%1").arg(m_settings.monitor.nProcessId)).exists();
#endif
}

//----------------------------------------------------------------------------
bool QtdSync::isServerRunning()
{
    bool bRet = false;
#ifdef WIN32
    QMap<unsigned long, QString> procs = QtdTools::getAllProcesses(EXEC("rsync"));
    bRet = procs.contains(m_settings.monitor.nServerProcessId);
//#else
//    bRet = QDir(QString("/proc/%1").arg(m_settings.monitor.nServerProcessId)).exists();
//#endif

    if (!bRet && m_settings.monitor.nServerProcessId != 0) {
        setupConf(true);
        m_settings.monitor.nServerProcessId = 0;
    } else if (!bRet && QFile(m_serverSettings.path + "/rsync.pid").exists()) {
        QString content = QtdTools::readFile(m_serverSettings.path + "/rsync.pid");
        if (content != "" && procs.contains(content.toULong())) {
            setCurrentServerPid(content.toULong());
        }
    }
#endif
    return bRet;
}

//----------------------------------------------------------------------------
bool QtdSync::setupConf(bool bDelete)
{
    bool bRet = true;
    if (bDelete) {
        QFile::remove(m_serverSettings.path + "/rsync.conf");
        QFile::remove(m_serverSettings.path + "/rsync.secrets");
        QFile::remove(m_serverSettings.path + "/rsync.pid");
    } else {
        QFile file(m_serverSettings.path + "/rsync.conf");
        if ((bRet &= file.open(QIODevice::WriteOnly))) {
            QTextStream ts(&file);
            ts << "use chroot = no\r\n";
            ts << "pid file = rsync.pid\r\n";
            ts << "strict modes = false\r\n";
            ts << "gid = 0\r\n";
            ts << "uid = 0\r\n";
            ts << "\r\n";
            
            QStringList users;

            for (int i = 0; i < m_serverSettings.dirs.count(); i++) {
                VirtualDirConfig dir = m_serverSettings.dirs.at(i);

                ts << QString("[%1]\r\n").arg(dir.name);

                QString path = evaluateString(dir.path);
                path.replace("\\","/");
#ifdef WIN32
                if (path.at(1) == ':') {
                    path = "/cygdrive/" + QString(path.at(0)) + path.mid(2);
                }
#endif

                ts << QString("    path = %1\r\n").arg(path);
                ts << QString("    read only = %1\r\n").arg(dir.bReadOnly ? "yes" : "no");
                if (dir.description != "") {
                    ts << QString("    comment = %1\r\n").arg(dir.description);
                }

                if (dir.users.count() > 0) {
                    ts << QString("    auth users = %1\r\n").arg(dir.users.join(", "));
                    ts << QString("    secrets file = rsync.secrets\r\n");
                    
                    foreach (QString user, dir.users) {
                        if (!users.contains(user)) users << user;
                    }
                }
                
                ts << "\r\n";
            }
            file.close();

            file.setFileName(m_serverSettings.path + "/rsync.secrets");
            if (bRet && users.count() > 0 && (bRet &= file.open(QIODevice::WriteOnly))) {
                ts.setDevice(&file);
                foreach (QString user, users) {
                    ts << QString("%1:%2\r\n").arg(user).arg(m_serverSettings.users[user]);
                }
                ts << "\r\n";
                file.close();
            }
        }
    }

    return bRet;
}


//----------------------------------------------------------------------------
void QtdSync::runOrStopServer(EnServerRunMode mode)
{
#ifdef WIN32
    getCurrentServerPid();
    if (!isServerRunning()) {
        if (mode & eStartOnly) {
            m_serverSettings.path = m_binPath;
            setupConf(true);
            setupConf();
            QDir::setCurrent(m_serverSettings.path);  

            checkFstab();

            ShellExecuteA(NULL, "open", "rsync.exe", "--config=rsync.conf --daemon", "", SW_HIDE);
            Sleep(1000);
            QString content = QtdTools::readFile("rsync.pid");
            if (content != "") {
                setCurrentServerPid(content.toULong());
            }
            getCurrentServerPid();
        }
    } else if (mode & eStopOnly) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, m_settings.monitor.nServerProcessId);
        if (hProcess != NULL) {
            TerminateProcess(hProcess, 0);
            CloseHandle(hProcess);
            getCurrentServerPid();
        }
        if (m_currentDlg == eServer && m_serverSettings.path != m_binPath) {
            loadConfig();
            updateServerUsers();
            updateServerDirectories();
        }
    }
    saveConfig();
#endif
}

//----------------------------------------------------------------------------
void QtdSync::updateServerUsers()
{
    uiServer.m_pCB_Users->clear();
    foreach (QString user, m_serverSettings.users.keys()) {
        uiServer.m_pCB_Users->addItem(QIcon(":/images/user.png"), user);
    }
}


//----------------------------------------------------------------------------
void QtdSync::updateServerDirectories(int nTopId)
{
    QTreeWidgetItem* pTempItem = 0L;
    if (nTopId == -1) {
        uiServer.m_pTW_Folders->clear();
        pTempItem = new QTreeWidgetItem(uiServer.m_pTW_Folders, QStringList(tr("Updating.") + tr("Please wait...")));
    }

    int nStartIdx = nTopId != -1 ? nTopId : 0;
    int nEndIdx   = nTopId != -1 ? nTopId + 1 : m_serverSettings.dirs.count();

    for (int i = nStartIdx; i < nEndIdx; i++) {
        VirtualDirConfig dir = m_serverSettings.dirs.at(i);

        QTreeWidgetItem* pTWI = 0L;
        if (nTopId != -1 && nTopId < uiServer.m_pTW_Folders->topLevelItemCount()) {
            pTWI = uiServer.m_pTW_Folders->topLevelItem(nTopId);
            pTWI->takeChildren();
        } else {
            pTWI = new QTreeWidgetItem(uiServer.m_pTW_Folders, QStringList(dir.name));
            pTWI->setHidden(true);
        }

        QString evalFolder = evaluateString(dir.path);
        pTWI->setText(0, dir.name + (dir.bReadOnly ? tr(" (read only!)") : ""));
        pTWI->setIcon(0,QIcon(dir.bReadOnly ? ":/images/cube_red.png" : ":/images/icon_green.png"));
        pTWI->setData(0, Qt::UserRole | 0x01, -1);
        pTWI->setData(0, Qt::UserRole | 0x02, true);

        QTreeWidgetItem* pFolder = new QTreeWidgetItem(pTWI, QStringList(evaluateString(dir.path, true)));
        pFolder->setData(0, Qt::UserRole, evalFolder);
        pFolder->setData(0, Qt::UserRole | 0x01, i);
        pFolder->setData(0, Qt::UserRole | 0x02, false);

        pTWI->setExpanded(true);

        if (dir.users.count() > 0) {
            QTreeWidgetItem* pUsers = new QTreeWidgetItem(pTWI, QStringList(tr("with allowed users")));
            QFont font = pUsers->font(0);
            font.setBold(true);
            pUsers->setFont(0, font);
            pUsers->setData(0, Qt::UserRole | 0x01, -1);
            pUsers->setData(0, Qt::UserRole | 0x02, true);
            foreach (QString user, dir.users) {
                pTWI = new QTreeWidgetItem(pUsers, QStringList(user));
                pTWI->setData(0, Qt::UserRole | 0x01, -1);
                pTWI->setData(0, Qt::UserRole | 0x02, true);
                pTWI->setIcon(0, QIcon(":/images/user.png"));
            }
            pUsers->setExpanded(true);
        }

        fillTreeElement(pFolder, false, 1);
    }

    if (pTempItem) {
        delete pTempItem;
    }
    for (int i = 0; i < uiServer.m_pTW_Folders->topLevelItemCount(); i++) {
        uiServer.m_pTW_Folders->topLevelItem(i)->setHidden(false);
    }
}

//----------------------------------------------------------------------------
void QtdSync::updateServerStatus()
{
    bool bIsServerRunning = isServerRunning();
    if (m_currentDlg == eServer) {
        uiServer.m_pPB_RunServer->setIcon(QIcon(bIsServerRunning ? ":/images/cube_red.png" : ":/images/icon_green.png"));
        uiServer.m_pPB_RunServer->setText(bIsServerRunning ? tr("Stop Server") : tr("Start Server"));

        uiServer.m_pL_Status_Icon->setPixmap(QPixmap(!bIsServerRunning ? ":/images/cube_red.png" : ":/images/icon_green.png"));
        uiServer.m_pL_Status_Text->setText(bIsServerRunning ? tr("Server is Running.") : tr("Server is Stopped!"));
    } else if (m_currentDlg == eTray) {
        QString iconFile = m_settings.monitor.bActive ? ":/images/schedule" : ":/images/schedule_off";
        if (bIsServerRunning) {
            iconFile += "_server";
        }
        m_pTrayIcon->setIcon(QIcon(iconFile + ".png"));
        QString toolTip = "QtdSync Monitor: " + (m_settings.monitor.bActive ? tr("Active"):tr("Not Active"));
#ifdef WIN32
        toolTip += "\nQtdSync Server: " + (bIsServerRunning ? tr("Running.") : tr("Stopped!"));
#endif
        m_pTrayIcon->setToolTip(toolTip);

    }
}

//----------------------------------------------------------------------------
// slots
//----------------------------------------------------------------------------
void QtdSync::slot_runServer()
{
    runOrStopServer();
}

//----------------------------------------------------------------------------
QString QtdSync::realDestinationFolder(QString folderName)
{
    QString destText = m_config.destination.dest;
    QString folderText = m_config.name;
    if (m_config.destination.bRemote) {
        folderText.replace(" ", "_");
        if (m_config.destination.bUseSSH) {
            destText = "ssh://" + destText;
        } else {
            destText = "rsync://" + destText;
        }

        if (!destText.endsWith("/") && !m_config.destination.path.startsWith("/")) destText += "/";
        destText += m_config.destination.path;

        if (!destText.endsWith("/") && !folderText.startsWith("/")) destText += "/";
        destText += folderText;

        if (!folderName.isEmpty()) {
            destText += "/" + folderName;
        }

    } else {
        destText = evaluateString(m_config.destination.dest, false);
#ifdef WIN32
        destText.replace("/", "\\");
        if (!destText.endsWith("\\")) destText += "\\" + folderText;
        if (!folderName.isEmpty()) {
            destText += "\\" + folderName;
        }
#else
        destText.replace("\\", "//");
        if (!destText.endsWith("/")) destText += "/" + folderText;
        if (!folderName.isEmpty()) {
            destText += "/" + folderName;
        }
#endif
    }

    return destText;
        
}

//----------------------------------------------------------------------------
void QtdSync::slot_subfolderStateChanged(int, bool bShowMessage)
{
    m_config.bCreateSubfolder = m_pChB_SubFolder->isChecked();
    if (!m_config.bCreateSubfolder) {
        if (bShowMessage) QMessageBox::warning(m_pDlg, tr("Subfolder disabled"), tr("You decided NOT to backup into a subfolder!<br><br>The backup of another backup set into the same backup destination will likely <b>corrupt your backup</b>!<br>Please make sure to use your backup destination only for <b>this</b> backup set!"));
        m_pL_RealDestination->setText("");
    } else {
        m_pL_RealDestination->setText(QString("(%1)").arg(realDestinationFolder()));
        
    }

}

//----------------------------------------------------------------------------
void QtdSync::slot_nameChanged(QString text)
{
    if (m_pCB_Name->itemData(m_pCB_Name->currentIndex()).toString() != m_config.fileName) {
        return;
    }

    if (m_config.name == text) {
        return;
    }

    //int id = 0;
    //QString origText = text;
    //int nCount = 1;

    //while ((id = m_pCB_Name->findText(text)) != -1) {
    //    text = QString("%1 (%2)").arg(origText).arg(nCount);
    //    nCount++;
    //}

    m_config.name = text;
    //if (m_pCB_Name->currentText() != text) {
    //    m_pCB_Name->setItemText(m_pCB_Name->currentIndex(), text);
    //}

}

//----------------------------------------------------------------------------
void QtdSync::slot_nameSelected(int id)
{
    QString fileName;

    if (id >= 0 && id < m_pCB_Name->count()) {
        fileName = m_pCB_Name->itemData(id).toString();
        if (!saveQtd()) {
            if (QMessageBox::question(m_pDlg, tr("File not saved"), tr("File <b>%1</b> not saved! Discard file?").arg(m_config.fileName), QMessageBox::Yes|QMessageBox::No) == QMessageBox::No) {
                return;
            }
        }

        if (fileName != "") {
            if (openQtd(fileName)) {
                initDlg();
            }
        }
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_typeChanged(int type)
{
    m_config.type = (BackupType)type;
}

//----------------------------------------------------------------------------
bool QtdSync::openQtd(QString fileName)
{
    if (readQtd(QtdTools::readFile(fileName), fileName)) {
        m_settings.lastModified[m_config.uuid] = QFileInfo(fileName).lastModified();
        m_config.fileName = fileName;
        return true;
    }

    return false;
}

//----------------------------------------------------------------------------
QString QtdSync::getQtdUuid(QString fileName)
{
    QString content = QtdTools::readFile(fileName);

    QDomDocument curDom;
    if (!curDom.setContent(content)) {
        return "";
    }

    QDomNode root = curDom.firstChildElement("QtdSync");
    if (root.isNull()) {
        return "";
    }

    QDomNamedNodeMap rAtts = root.attributes();
    if (rAtts.contains("uuid")) {
        return rAtts.namedItem("uuid").nodeValue();
    } else {
        return "";
    }
}


//----------------------------------------------------------------------------
QString QtdSync::getQtdName(QString fileName)
{
    QString content = QtdTools::readFile(fileName);

    QDomDocument curDom;
    if (!curDom.setContent(content)) {
        return "";
    }

    QDomNode root = curDom.firstChildElement("QtdSync");
    if (root.isNull()) {
        return "";
    }

    QDomNamedNodeMap rAtts = root.attributes();
    return rAtts.namedItem("name").nodeValue();
}

//----------------------------------------------------------------------------
bool QtdSync::setQtdUuid(QString fileName, QString uuid)
{
    QString content = QtdTools::readFile(fileName);
    if (content == "") {
        return false;
    }

    QDomDocument curDom;
    if (!curDom.setContent(content)) {
        return false;
    }

    QDomElement root = curDom.firstChildElement("QtdSync");
    if (root.isNull()) {
        return false;
    }

    root.setAttribute("uuid", uuid);

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream ts(&file);
        ts << curDom.toString(4);
        file.close();
        return true;
    }
    return false;

}

//----------------------------------------------------------------------------
void QtdSync::localDestinationMissing()
{
    QString evalFolder = evaluateString(m_config.destination.dest);
    m_pUpdateDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);
    Ui::QtdSyncFolderBinding bUi;

    bool bBoundToQtdSync = m_config.destination.dest.contains("%QtdSync%");
    bool bBoundToQtd     = m_config.destination.dest.contains("%Qtd%");
    bool bBoundNoWhere   = !bBoundToQtdSync && !bBoundToQtd;

    bUi.SetupUi(m_pUpdateDlg);
    bUi.m_pCaption->setText(bBoundNoWhere ? tr("Folder not found") : tr("Error on folder binding"));
    bUi.m_pBindToQtdSync->setVisible(false);
    bUi.m_pAbsoluteBind->setVisible(false);
    bUi.m_pBindToQtd->setVisible(true);
    bUi.m_pBindToQtd->setText(tr("Ignore this error"));
    bUi.m_pBindToQtd->setIcon(QIcon(":/images/delete.png"));
    bUi.m_pAbsolute->setText(tr("Relocated folder"));
    bUi.m_pBottomLine->hide();
    bUi.m_pBottomLine2->hide();
    bUi.m_pBindToSelectedQtd->hide();
    bUi.m_pCB_QtdFiles->hide();
    bUi.m_pAbsoluteBind->hide();
    bUi.m_pBottomLine3->hide();
    bUi.m_pChB_DoNotAskAgain->hide();

    QString driveLetter = bBoundToQtd ? QFileInfo(m_config.fileName).absoluteFilePath().mid(0,1) : qApp->applicationDirPath().mid(0,1);
    if (!bBoundNoWhere) {
        bUi.m_pInfo->setText(tr("You have bound the backup destination folder<br><center><b>%1</b></center><br>to the drive where %2 is located.<br>But this folder does not exist on the drive <b>%3:</b>.").arg(evalFolder.mid(2)).arg(bBoundToQtdSync ? "QtdSync" : tr("the backup set file")).arg(driveLetter));
    } else {
        bUi.m_pInfo->setText(tr("The backup destination folder<br><center><b>%1</b></center><br>does not exist.").arg(evalFolder));
    }

    connect(bUi.m_pBindToQtd,       SIGNAL(clicked()), m_pUpdateDlg, SLOT(accept()));

    qApp->processEvents();
    m_pUpdateDlg->adjustSize();

    int ret = m_pUpdateDlg->exec();
    delete m_pUpdateDlg;
    m_pUpdateDlg = 0L;

    if (ret == QDialog::Rejected) {
        QString dest = browseForFolder("", false, tr("Select the backup destination folder"));
        if (dest != "") {
            m_config.destination.dest = dest;
        }
    }
}


//----------------------------------------------------------------------------
void QtdSync::slot_openFile()
{
    if (qtdHasChanged()) {
        if (QMessageBox::question(m_pDlg, tr("Backup set changed"), tr("The current backup set has been changed! Do you want to save it?"), QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes) {
            if (!saveQtd()) {
                if (QMessageBox::question(m_pDlg, tr("File not saved"), tr("File <b>%1</b> not saved! Discard file?").arg(m_config.fileName), QMessageBox::Yes|QMessageBox::No) == QMessageBox::No) {
                    return;
                }
            }
        }
    }


    QString fileName = QFileDialog::getOpenFileName(m_pDlg, tr("Open %1 Backup set file").arg(m_productName), "", tr("Qtd-File (*.qtd)"));
    if (fileName != "") {
        if (openQtd(fileName)) {
            initDlg();
        }
    }

}

//----------------------------------------------------------------------------
void QtdSync::slot_browseExecPath()
{
    QString path = QFileDialog::getOpenFileName(0L, tr("Select Executable"), "", tr("Executables (*.exe *.com);;Scripts (*.bat *.cmd *.sh);;Any (*.*)"));
    if (path != "") {
        path = browseForFolder(path, true);
        if (path != "") {
            m_tmpString2 = path;
            uiConfig.m_pLE_Folder->setText(evaluateString(path, true));
        }
    }
}

//----------------------------------------------------------------------------
bool QtdSync::editProcessingScript(bool bPre, QString& path, QString& args)
{
    QDialog* eDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);
    bool bReturn = false;
    m_tmpString2 = path;
    uiConfig.SetupUi(eDlg);

    if (bPre) {
        uiConfig.m_pL_Caption->setText(tr("Backup Set Preprocessing"));
    } else {
        uiConfig.m_pL_Caption->setText(tr("Backup Set Postprocessing"));

    }

    uiConfig.m_pLE_Folder->setText(evaluateString(path, true));
    uiConfig.m_pLE_Name->setText(args);
    uiConfig.m_pGB_Excludes->hide();
    uiConfig.m_pGB_SpecialFeatures->hide();

    uiConfig.m_pL_Folder->setText(tr("Execute"));
    uiConfig.m_pL_Name->setText(tr("Arguments"));

    connect(uiConfig.m_pPB_Browse, SIGNAL(clicked()), this, SLOT(slot_browseExecPath()));

    qApp->processEvents();
    eDlg->adjustSize();

    if ((bReturn = (eDlg->exec() == QDialog::Accepted))) {
        args = uiConfig.m_pLE_Name->text();
        path = m_tmpString2;
    }
    delete eDlg;

    return bReturn;
}







//----------------------------------------------------------------------------
void QtdSync::slot_excludeEdited(const QString& text) 
{
    QStringList excludes;
    for (int i = 0; i < uiConfig.m_pCB_Exclude->count(); i++) {
        excludes << uiConfig.m_pCB_Exclude->itemText(i);
    }
    uiConfig.m_pPB_AddExclude->setEnabled(!excludes.contains(text) && text != "");
    uiConfig.m_pPB_RemoveExclude->setEnabled(excludes.contains(text));
}

//----------------------------------------------------------------------------
void QtdSync::slot_excludeAdd()
{
    if (uiConfig.m_pCB_Exclude->currentText() == "") {
        return;
    }

    QStringList excludes;
    for (int i = 0; i < uiConfig.m_pCB_Exclude->count(); i++) {
        excludes << uiConfig.m_pCB_Exclude->itemText(i);
    }
    QStringList currentExcludes = uiConfig.m_pCB_Exclude->currentText().split(";", QString::SkipEmptyParts);
    foreach (QString curEx, currentExcludes) {
        if (!excludes.contains(curEx)) {
            uiConfig.m_pCB_Exclude->addItem(curEx);
        }
    }
    uiConfig.m_pPB_AddExclude->setEnabled(false);
    uiConfig.m_pPB_RemoveExclude->setEnabled(uiConfig.m_pCB_Exclude->count() > 0);
}

//----------------------------------------------------------------------------
void QtdSync::slot_excludeRemove()
{
    uiConfig.m_pCB_Exclude->removeItem(uiConfig.m_pCB_Exclude->currentIndex());
}

//----------------------------------------------------------------------------
void QtdSync::slot_browseFolderPath()
{
    QString folder = browseForFolder();
    if (folder != "") {
        m_tmpString2 = folder;
        uiConfig.m_pLE_Folder->setText(evaluateString(folder, true));
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_configRsyncReset()
{
    uiConfig.m_pLE_SpecialFeatures->setText(m_config.globalRsyncOptions);
}

//----------------------------------------------------------------------------
void QtdSync::slot_configSubfolderStateChanged(int, bool bShowMessage)
{
    if (!uiConfig.m_pChB_SubFolder->isChecked()) {
        if (bShowMessage) QMessageBox::warning(m_pDlg, tr("Subfolder disabled"), tr("You decided NOT to backup into a subfolder!<br><br>This is only allowed if you have only one folder to backup in your backup set.<br><b>If you add another folder this will be enabled again!</b>"));
        uiConfig.m_pL_RealDestination->setText(QString("%1").arg(realDestinationFolder()));
    } else {
        uiConfig.m_pL_RealDestination->setText(QString("%1").arg(realDestinationFolder(uiConfig.m_pLE_Name->text())));
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_configSubfolderNameChanged(QString)
{
    slot_configSubfolderStateChanged(0, false);
}

//----------------------------------------------------------------------------
QString priv_QtdSync_getUniqueFolderName(QList<QtdSync::Folder>& folderList, QString name, int nCurrentIndex = -1)
{
    QStringList fldNames;
    int i = 0;
    foreach (QtdSync::Folder fold, folderList) {
        if (i != nCurrentIndex) {
            fldNames << fold.name;
        }
        i++;
    }

    QString fixedName = name;
    i = 1;
    while (fldNames.contains(fixedName)) {
        i++;
        fixedName = QString("%1_%2").arg(name).arg(i);
    }

    return fixedName;
}

//----------------------------------------------------------------------------
bool QtdSync::editFolder(Folder& fold)
{
    QDialog* eDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);
    bool bReturn = false;
    uiConfig.SetupUi(eDlg);

    m_tmpString2 = fold.folder;
    uiConfig.m_pLE_Folder->setText(evaluateString(fold.folder, true));
    uiConfig.m_pLE_Name->setText(fold.name);

    uiConfig.m_pCB_Exclude->clear();
    QStringList excludes = fold.exclude.split(";", QString::SkipEmptyParts);
    uiConfig.m_pCB_Exclude->addItems(excludes);
    uiConfig.m_pPB_RemoveExclude->setEnabled(excludes.count() > 0);
    uiConfig.m_pChB_SubFolder->setChecked(fold.bCreateSubFolder || m_config.folders.count() > 1);
    uiConfig.m_pChB_SubFolder->setVisible(m_config.folders.count() <= 1);
    connect(uiConfig.m_pChB_SubFolder, SIGNAL(stateChanged(int)), this, SLOT(slot_configSubfolderStateChanged(int)));

    if (!m_settings.showRsyncExpertSettings) {
        uiConfig.m_pGB_SpecialFeatures->hide();
    } else {
        uiConfig.m_pLE_SpecialFeatures->setCompleter(m_pRsyncCompleter);
        if (fold.rsyncExpert.contains("use-global-settings")) {
            uiConfig.m_pGB_SpecialFeatures->setChecked(false);
            fold.rsyncExpert = m_config.globalRsyncOptions;
        } else {
            uiConfig.m_pGB_SpecialFeatures->setChecked(true);
        }
        uiConfig.m_pLE_SpecialFeatures->setText(fold.rsyncExpert);
        connect(uiConfig.m_pPB_ResetRsync, SIGNAL(clicked()), this, SLOT(slot_configRsyncReset()));
    }
    qApp->processEvents();
    eDlg->adjustSize();

    uiConfig.m_pChB_DeleteExcluded->setChecked(fold.rsyncExpert.contains("--delete-excluded"));

    connect(uiConfig.m_pCB_Exclude, SIGNAL(editTextChanged(const QString&)), this, SLOT(slot_excludeEdited(const QString&)));
    connect(uiConfig.m_pPB_AddExclude, SIGNAL(clicked()), this, SLOT(slot_excludeAdd()));
    connect(uiConfig.m_pPB_RemoveExclude, SIGNAL(clicked()), this, SLOT(slot_excludeRemove()));
    connect(uiConfig.m_pPB_Browse, SIGNAL(clicked()), this, SLOT(slot_browseFolderPath()));
    connect(uiConfig.m_pLE_Name, SIGNAL(textChanged(QString)), this, SLOT(slot_configSubfolderNameChanged(QString)));

    slot_configSubfolderStateChanged(0, false);

    while ((bReturn = (eDlg->exec() == QDialog::Accepted)) && uiConfig.m_pLE_Name->text() == "") {
        QMessageBox::warning(m_pDlg, tr("Warning"), tr("Please define a <b>Name</b>!"));
        uiConfig.m_pL_Name->setStyleSheet("color: red;");
    }

    if (bReturn) {
        fold.name = uiConfig.m_pLE_Name->text();
        
        fold.bCreateSubFolder = uiConfig.m_pChB_SubFolder->isChecked();
        QStringList excludes;
        for (int i = 0; i < uiConfig.m_pCB_Exclude->count(); i++) {
            excludes << uiConfig.m_pCB_Exclude->itemText(i);
        }
        if (m_settings.showRsyncExpertSettings) {
            if (uiConfig.m_pGB_SpecialFeatures->isChecked()) {
                fold.rsyncExpert = uiConfig.m_pLE_SpecialFeatures->text();
            } else {
                fold.rsyncExpert = "use-global-settings";
            }
        }

        if (uiConfig.m_pChB_DeleteExcluded->isChecked()) {
            if (!fold.rsyncExpert.contains("--delete-excluded")) {
                fold.rsyncExpert += " --delete-excluded";
            }
        } else {
            fold.rsyncExpert.replace(" --delete-excluded", "");
        }

        fold.exclude = excludes.join(";");
        fold.folder = m_tmpString2;
    }
    delete eDlg;

    return bReturn;
}

//----------------------------------------------------------------------------
void QtdSync::slot_browseVDirPath()
{
    QString path = browseForFolder();
    if (path != "") {
        m_tmpString2 = path;
        uiVDirConfig.m_pLE_Folder->setText(evaluateString(path, true));
    }
}   


//----------------------------------------------------------------------------
bool QtdSync::editVirtualDir(VirtualDirConfig& dir)
{
    QDialog* eDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);
    bool bReturn = false;

    uiVDirConfig.SetupUi(eDlg);

    m_tmpString2 = dir.path;
    uiVDirConfig.m_pLE_Folder->setText(evaluateString(dir.path, true));
    uiVDirConfig.m_pLE_Name->setText(dir.name);
    uiVDirConfig.m_pCB_Users->setEmptyDisplayText("<" + tr("anonymous") + ">");

    connect(uiVDirConfig.m_pPB_Browse, SIGNAL(clicked()), this, SLOT(slot_browseVDirPath()));

    qApp->processEvents();
    eDlg->adjustSize();

    uiVDirConfig.m_pChB_ReadOnly->setChecked(dir.bReadOnly);

    foreach (QString user, m_serverSettings.users.keys()) {
        uiVDirConfig.m_pCB_Users->addItem(user, dir.users.contains(user));
    }

    if ((bReturn = (eDlg->exec() == QDialog::Accepted))) {
        dir.name = uiVDirConfig.m_pLE_Name->text();
        dir.path = m_tmpString2;
        dir.bReadOnly = uiVDirConfig.m_pChB_ReadOnly->isChecked();

        dir.users.clear();

        int i = 0;
        foreach (QString user, m_serverSettings.users.keys()) {
            if (uiVDirConfig.m_pCB_Users->isChecked(i)) {
                dir.users << uiVDirConfig.m_pCB_Users->itemText(i);
            }
            i++;
        }
    }
    delete eDlg;
    return bReturn;
}

//----------------------------------------------------------------------------
void QtdSync::slot_saveAs()
{
    m_config.unSaved = true;
    m_config.isNew = true;
    saveQtd();
}

//----------------------------------------------------------------------------
void QtdSync::slot_save()
{
    saveQtd();
}

//----------------------------------------------------------------------------
void QtdSync::slot_newFileCopy()
{
    if (qtdHasChanged()) {
        if (QMessageBox::question(m_pDlg, tr("Backup set changed"), tr("The current backup set has been changed! Do you want to save it?"), QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes) {
            if (!saveQtd()) {
                if (QMessageBox::question(m_pDlg, tr("File not saved"), tr("File <b>%1</b> not saved! Discard file?").arg(m_config.fileName), QMessageBox::Yes|QMessageBox::No) == QMessageBox::No) {
                    return;
                }
            }
        }
    }


    QString fileName = QFileDialog::getOpenFileName(m_pDlg, tr("Create %1 Backup set copy of...").arg(m_productName), "", tr("Qtd-File (*.qtd)"));
    if (fileName != "") {
        if (openQtd(fileName)) {
            m_config.isNew          = true;
            m_config.unSaved        = true;
            m_config.name           = tr("Copy of %1").arg(m_config.name);
            m_config.fileName       = "";
            m_config.lastBackup     = QDateTime::fromString("");
            m_config.lastBackupTry  = QDateTime::fromString("");
            m_config.uuid           = QUuid::createUuid().toString();

            initDlg();
        }
    }

}

//----------------------------------------------------------------------------
void QtdSync::slot_newFile()
{
    if (m_currentDlg == eMain) {
        if (qtdHasChanged()) {
            if (QMessageBox::question(m_pDlg, tr("Backup set changed"), tr("The current backup set has been changed! Do you want to save it?"), QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes) {
                if (!saveQtd()) {
                    if (QMessageBox::question(m_pDlg, tr("File not saved"), tr("File <b>%1</b> not saved! Discard file?").arg(m_config.fileName), QMessageBox::Yes|QMessageBox::No) == QMessageBox::No) {
                        return;
                    }
                }
            }
        }
    } else if (m_currentDlg == eStartup) {
        if (m_timer) m_timer->stop();
    }

    clearQtd();

    if (m_currentDlg == eStartup) {
        delete m_pDlg;
        m_pDlg = 0L;

        showDlg(eMain);
        initDlg();

        m_timer->start(500);
        slot_checkForUpdate();
    }

    initDlg();
}

//----------------------------------------------------------------------------
void QtdSync::slot_editFolder()
{
    if (m_currentDlg == eMain) {
        QTreeWidgetItem* pCurIt = m_pTW_Folders->currentItem();
        if (!pCurIt) return;

        while (pCurIt->parent() != 0L) pCurIt = pCurIt->parent();

        if (!pCurIt) return;

        int nFolderCount = 0;
        for (int i = 0; i < m_pTW_Folders->topLevelItemCount(); i++) {
            if (m_pTW_Folders->topLevelItem(i) == pCurIt) {
                bool bReturn = false;
                if (pCurIt->data(0, Qt::UserRole | 0x03).toInt() == 0) {
                    Folder fold = m_config.folders.at(nFolderCount);
                    bReturn = editFolder(fold);
                    fold.name = priv_QtdSync_getUniqueFolderName(m_config.folders, fold.name, nFolderCount);
                    m_config.folders.replace(nFolderCount, fold);
                } else if (pCurIt->data(0, Qt::UserRole | 0x03).toInt() == 1) {
                    bReturn = editProcessingScript(true, m_config.preProcessing.first, m_config.preProcessing.second);
                } else if (pCurIt->data(0, Qt::UserRole | 0x03).toInt() == 2) {
                    bReturn = editProcessingScript(false, m_config.postProcessing.first, m_config.postProcessing.second);
                }

                if (bReturn) initDlg();
                break;
            }
            if (m_pTW_Folders->topLevelItem(i)->data(0, Qt::UserRole | 0x03).toInt() == 0) {
                    nFolderCount++;
            }
        }
    } else if (m_currentDlg == eStartup) {
        if (m_timer) m_timer->stop();

        QString fileName("");
        QTreeWidgetItem* pCurIt = uiStartup.m_pTW_Sets->currentItem();
        if (pCurIt) {
            while (pCurIt->parent() != 0L) pCurIt = pCurIt->parent();
            fileName = pCurIt->data(0, Qt::UserRole).toString();
        }

        openQtd(fileName);

        m_pDlg->hide();
        m_pDlg->deleteLater();
        m_pDlg = 0L;

        showDlg(eMain);
        initDlg();

        m_timer->start(500);
    } else if (m_currentDlg == eServer) {
        QTreeWidgetItem* pCurIt = uiServer.m_pTW_Folders->currentItem();
        if (!pCurIt) return;

        while (pCurIt->parent() != 0L) pCurIt = pCurIt->parent();

        if (!pCurIt) return;

        for (int i = 0; i < uiServer.m_pTW_Folders->topLevelItemCount(); i++) {
            if (uiServer.m_pTW_Folders->topLevelItem(i) == pCurIt) {
                VirtualDirConfig dir = m_serverSettings.dirs.at(i);
                if (editVirtualDir(dir))  {
                    m_serverSettings.dirs.replace(i, dir);
                    updateServerDirectories(i);
                    saveConfig();
                }
                break;
            }
        }
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_removeBackupSet()
{
    if (m_currentDlg == eStartup) {
        QString fileName("");
        QTreeWidgetItem* pCurIt = uiStartup.m_pTW_Sets->currentItem();
        if (pCurIt) {
            while (pCurIt->parent() != 0L) pCurIt = pCurIt->parent();
            fileName = pCurIt->data(0, Qt::UserRole).toString();
        }

        if (QMessageBox::question(m_pDlg, tr("Delete Backup set"), tr("Do you really want to delete the backup set<br><b>%1</b>?").arg(fileName), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            QFile::remove(fileName);
            m_settings.knownQtd.remove(fileName);
            saveConfig();

            fillStartupDlg(m_settings.knownQtd.keys());
        }
    }
}

//----------------------------------------------------------------------------
bool QtdSync::editUser(QString& user, QString& password)
{
    QDialog* pDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);
    uiRemoteUser.SetupUi(pDlg);
    bool bReturn = false;

    uiRemoteUser.m_pL_Caption->setText(tr("QtdSync Server User"));
    uiRemoteUser.m_pLine_Upper->hide();
    uiRemoteUser.m_pL_Info->hide();
    uiRemoteUser.m_pL_BackupSet->hide();
    uiRemoteUser.m_pL_Info2->hide();
    uiRemoteUser.m_pL_Server->hide();
    uiRemoteUser.m_pLE_Server->hide();
    uiRemoteUser.m_pGB_UserAuth->setChecked(true);
    uiRemoteUser.m_pGB_UserAuth->setCheckable(false);
    uiRemoteUser.m_pGB_UserAuth->setTitle(tr("User"));
    uiRemoteUser.m_pPB_BrowseKeyFile->hide();
    uiRemoteUser.m_pChB_UseSSH->hide();
    uiRemoteUser.m_pChB_SavePassword->hide();

    if (user != "") {
        uiRemoteUser.m_pLE_Name->setText(user);
        uiRemoteUser.m_pLE_Name->setReadOnly(true);
        uiRemoteUser.m_pLE_Password->setText(password);
    }

    qApp->processEvents();
    pDlg->adjustSize();

    if ((bReturn = (pDlg->exec() == QDialog::Accepted))) {
        QString userName = uiRemoteUser.m_pLE_Name->text();
        password = uiRemoteUser.m_pLE_Password->text();

        if (userName != "") {
            if (m_serverSettings.users.contains(userName) && user == "") {
                QMessageBox::warning(pDlg, tr("Adding User"), tr("User <b>%1</b> already exists.").arg(user));
            } else {
                m_serverSettings.users[userName] = password;
                updateServerUsers();
                saveConfig();
            }
        }
    }

    delete pDlg;

    return bReturn;
}


//----------------------------------------------------------------------------
void QtdSync::slot_addUser()
{
    QString user, password;
    editUser(user, password);
}

//----------------------------------------------------------------------------
void QtdSync::slot_removeUser()
{
    QString user = uiServer.m_pCB_Users->currentText();
    if (QMessageBox::question(m_pDlg, tr("Delete User"), 
        tr("Do you really want to delete the user <b>%1</b>?<br><br>It will be removed from every virtual directorys allowed users!").arg(user),
        QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        m_serverSettings.users.remove(user);

        for (int i = 0; i < m_serverSettings.dirs.count(); i++) {
            VirtualDirConfig dir = m_serverSettings.dirs.at(i);
            dir.users.removeAll(user);
            m_serverSettings.dirs.replace(i, dir);
        }

        updateServerUsers();
        updateServerDirectories();

        saveConfig();
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_editUser()
{
    QString oldUser = uiServer.m_pCB_Users->currentText();
    QString user = oldUser;

    if (m_serverSettings.users.contains(user)) {
        QString password = m_serverSettings.users[user];
        editUser(user, password);
    }
}


//----------------------------------------------------------------------------
void QtdSync::slot_bindToComputer()
{
    m_tmpString = m_tmpString.at(0) + QString("%1").arg(m_settings.computerUuid) + m_tmpString.mid(1);
    m_pUpdateDlg->accept();
}

//----------------------------------------------------------------------------
void QtdSync::slot_bindToQtdSyncDrive()
{
    m_tmpString = "%QtdSync%" +m_tmpString.mid(2);
    m_pUpdateDlg->accept();
}

//----------------------------------------------------------------------------
void QtdSync::slot_bindToQtdDrive()
{
    if (QFileInfo(m_config.fileName).absoluteFilePath().at(0) != m_tmpString.at(0) || (m_config.isNew && m_config.fileName.at(1) != ':')) {
        m_config.fileName = QString("%1:/").arg(m_tmpString.at(0)) + QFileInfo(m_config.fileName).fileName();
    }

    m_tmpString = "%Qtd%" + m_tmpString.mid(2);
    m_pUpdateDlg->accept();
}

//----------------------------------------------------------------------------
void QtdSync::slot_bindToQtdFile()
{
    m_tmpString = "%QtdFile%" + m_tmpString.mid(2);
    m_pUpdateDlg->accept();
}

//----------------------------------------------------------------------------
QStringList QtdSync::remotePaths(Destination& destination)
{
    QTcpSocket* tcp     = new QTcpSocket(this);
    QUrl        url(QString("http://%1").arg(destination.dest));
    QString     host    = url.host();

    int         nPort   = url.port();
    bool        bUserAuth = false;

    if (nPort == -1) nPort = 873;

    if (m_config.destination.bUseSSH) nPort = 873;

    tcp->connectToHost(host, nPort);
    if (!tcp->waitForConnected(1000)) {
        tcp->disconnectFromHost();
        delete tcp;
        return QStringList();
    }
    tcp->disconnectFromHost();
    delete tcp;

    m_pProcess = new QProcess(this);
    m_pProcess->setWorkingDirectory(m_binPath);

    QStringList     args;
    QString         passWd;
    QStringList     ret;
    QStringList     env;

remotePathsTryAgain:

    env.clear();
    args.clear();

    passWd = destination.password;
    if (passWd == "") passWd = "dummypw";

    env << "RSYNC_PASSWORD=" + passWd;

    if (nPort != 873) {
        args << QString("--port=%1").arg(nPort);
    }

    if (destination.bUserAuth && destination.user != "") {
        host = destination.user + "@" + url.host();
    }

    args << host + "::";

    m_pProcess->setEnvironment(env);
    m_pProcess->start(PEXEC("rsync"), args);
    m_pProcess->waitForFinished(5000);

    if (m_pProcess->state() == QProcess::Running) {
        m_pProcess->terminate();
    } else if (m_pProcess->exitCode() != 0) {
        passWd = m_pProcess->readAllStandardError();
        if (!bUserAuth && (passWd.toLower().contains("auth failed") || passWd.toLower().contains("permission denied"))) {
            slot_setRemoteUserAuth();
            bUserAuth = true;
            goto remotePathsTryAgain;
        }
    } else {
        QString out = m_pProcess->readAllStandardOutput();
        
        delete m_pProcess;

        foreach (QString line, out.split("\n")) {
            if (line.trimmed() != "") {
                QString name = line.split("\t", QString::SkipEmptyParts)[0].trimmed();
                ret << name;
            }
        }
    }

    return ret;
}

//----------------------------------------------------------------------------
QString QtdSync::browseForFolder(QString tmpString, bool bIsFile, QString caption)
{
    if (caption == "") {
        caption = tr("Select the folder to backup");
    }

    if (tmpString == "" || !QFileInfo(tmpString).exists()) {
        m_tmpString = QFileDialog::getExistingDirectory(m_pDlg, caption, tmpString, QFileDialog::ShowDirsOnly);
    } else {
        m_tmpString = tmpString;
    }

#ifndef WIN32
    m_tmpString = "/" + m_tmpString.split("/", QString::SkipEmptyParts).join("/");
#endif

    if (m_tmpString != "" && m_settings.showFolderBindingOptions) {
#ifdef WIN32
        bool bSetQtdSync = m_tmpString.at(0).toLower() == qApp->applicationDirPath().at(0).toLower();
        bool bSetQtd     = (m_tmpString.at(0).toLower() == m_config.fileName.at(0).toLower() && m_config.fileName.toLower() == QFileInfo(m_config.fileName).absoluteFilePath().toLower()) ||
                          (m_config.isNew && m_config.fileName.at(1) != ':');
#else
        bool bSetQtdSync = false;
        bool bSetQtd     = false;
#endif

        if (m_serverSettings.bSelf) bSetQtd = false;

        QStringList qtdFiles;
        QString currentFolder = m_tmpString;

        foreach (QString qtd, QDir(currentFolder).entryList(QStringList("*.qtd"), QDir::Files)) {
            addToKnownQtd(currentFolder + "/" + qtd);
        }
#ifdef WIN32
        foreach (QString qtd, m_settings.knownQtd.keys()) {
            if (qtd.at(0) == m_tmpString.at(0) && qtd != m_config.fileName) {
                qtdFiles << qtd;
            }
        }
#endif
        bool bSetQtdFile = qtdFiles.count() > 0;

        if (bSetQtdSync || bSetQtd || true) {

            m_pUpdateDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);
            Ui::QtdSyncFolderBinding bUi;
            
            QString fileFolder = bIsFile ? QApplication::translate("QtdSyncSpecial", "File") : QApplication::translate("QtdSyncSpecial", "Folder");
            bUi.SetupUi(m_pUpdateDlg);

            if (bIsFile) {
                bUi.m_pCaption->setText(tr("Bind File"));
            }
            bUi.m_pBindToQtdSync->setVisible(bSetQtdSync);
            bUi.m_pBindToQtd->setVisible(bSetQtd);

            bUi.m_pBottomLine2->setVisible(bSetQtdFile);
            bUi.m_pBindToSelectedQtd->setVisible(bSetQtdFile);
            bUi.m_pCB_QtdFiles->setVisible(bSetQtdFile);
            bUi.m_pBottomLine->setVisible(bSetQtdSync || bSetQtd || bSetQtdFile);

            if (bSetQtdFile) {
                foreach (QString qtd, qtdFiles) {
                    bUi.m_pCB_QtdFiles->addItem(QFileInfo(qtd).fileName());
                }
            }

            QStringList locations;
            if (bSetQtdSync) locations << "<i>QtdSync</i>";
            if (bSetQtd)     locations << tr("this backup set file");
            if (bSetQtdFile) locations << tr("other backup set files");

            QString location = "<ul><li>" + locations.join(" " + tr("and") + "</li><li>") + "</li></ul>";

            if (bSetQtdSync || bSetQtd || bSetQtdFile) {
                bUi.m_pInfo->setText(tr("The selected %1<br><center><b>%2</b></center><br> is located at the same drive as %3Do you want to bind it").arg(fileFolder).arg(m_tmpString).arg(location));
            } else {
                bUi.m_pInfo->setText(tr("Do you want to bind the %1 <b>%2</b>").arg(fileFolder).arg(m_tmpString));
            }
            connect(bUi.m_pBindToSelectedQtd,SIGNAL(clicked()), this, SLOT(slot_bindToQtdFile()));
            connect(bUi.m_pBindToQtd,       SIGNAL(clicked()), this, SLOT(slot_bindToQtdDrive()));
            connect(bUi.m_pBindToQtdSync,   SIGNAL(clicked()), this, SLOT(slot_bindToQtdSyncDrive()));
            connect(bUi.m_pAbsoluteBind,    SIGNAL(clicked()), this, SLOT(slot_bindToComputer()));

            qApp->processEvents();
            m_pUpdateDlg->adjustSize();

            m_pUpdateDlg->exec();

            if (bUi.m_pChB_DoNotAskAgain->isChecked()) {
                QMessageBox::information(m_pDlg, tr("Disable Binding Options Dialog"), tr("You disabled the Binding Options dialog.<br><br>You can enable it under <b>Settings</b>."));
                m_settings.showFolderBindingOptions = false;
                saveConfig();
            }

            if (m_tmpString.startsWith("%QtdFile%")) {
                m_tmpString.replace("%QtdFile%", m_settings.knownQtd[qtdFiles.at(bUi.m_pCB_QtdFiles->currentIndex())]);
            }

            delete m_pUpdateDlg;
            m_pUpdateDlg = 0L;
        }
    }

    return m_tmpString;
}


//----------------------------------------------------------------------------
void QtdSync::slot_browseDestination(QString dest)
{
    dest = browseForFolder(dest, false, tr("Select the backup destination folder"));
    if (dest == "") return;

    m_config.destination.bRemote = false;
    m_pW_Remote->hide();
    m_pL_FreeSpace->show();
    m_pGB_BackupInfo_Local->show();
    m_config.destination.dest = dest;
    m_pLE_Destination->setText(evaluateString(m_config.destination.dest, true));
    m_pL_Destination->setText(tr("Destination"));

    QString destStr = evaluateString(m_config.destination.dest, false);
    updateFreeSpaceText(destStr);
    //slot_updateFileSizeEstimations();

    slot_subfolderStateChanged(0, false);

    m_pLE_Destination->setAcceptDrops(true);
    m_pLE_Destination->setReadOnly(false);
}

//----------------------------------------------------------------------------
void QtdSync::slot_sshDestEdited(const QString& text)
{
    m_config.destination.path = text;
    slot_subfolderStateChanged(0, false);
}

//----------------------------------------------------------------------------
void QtdSync::slot_destEdited()
{
    if (m_config.destination.bRemote) {
        if (m_pLE_Destination->text() != (m_config.destination.bUseSSH ? "ssh://" : "rsync://") + m_config.destination.dest) {
            m_config.destination.dest = m_pLE_Destination->text();
            if (m_config.destination.dest.startsWith("ssh:") || (m_config.destination.bUseSSH && !m_config.destination.dest.startsWith("rsync:"))) {
                m_config.destination.dest = m_config.destination.dest.replace(QRegExp("^ssh://"), "");
                slot_remoteSSHDestination(true);
            } else {
                m_config.destination.dest = m_config.destination.dest.replace(QRegExp("^rsync://"), "");
                slot_remoteDestination();
            }
        }
    } else {
        if (m_pLE_Destination->text() != evaluateString(m_config.destination.dest, true)) {
            m_config.destination.dest = m_pLE_Destination->text();
            QString evalFolder = evaluateString(m_config.destination.dest);
            if (!QDir(evalFolder).exists() && !(evalFolder.length() > 2 && evalFolder.at(1) == '{')) {
                localDestinationMissing();
            } else {
                slot_browseDestination(m_config.destination.dest);
            }
        }
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_pathSelected(const QString& text)
{
    m_config.destination.path = text;
}

//----------------------------------------------------------------------------
void QtdSync::slot_remoteDestination()
{
    if (!m_config.destination.bRemote || m_config.destination.bUseSSH) {
        m_config.destination.bRemote = true;
        m_config.destination.bUseSSH = false;
        if (!m_config.destination.bRemote) {
            m_config.destination.dest = "127.0.0.1";
        }
        m_config.destination.password = "";
        m_config.destination.path = "";
        slot_setRemoteUserAuth(true);
    }

    m_pW_Remote->show();
    m_pLE_RemotePath->hide();
    m_pCB_RemotePath->show();

    m_pL_FreeSpace->hide();
    m_pGB_BackupInfo_Local->hide();
    m_pL_Destination->setText(tr("Host"));
    m_pLE_Destination->setText("rsync://"+m_config.destination.dest);
    m_pLE_Destination->setReadOnly(false);
    m_pLE_Destination->setAcceptDrops(false);

    slot_subfolderStateChanged(0, false);

    m_pCB_RemotePath->show();
    QString curPath = m_config.destination.path;
    QStringList paths = remotePaths(m_config.destination);
    if (paths.count() > 0) {
        m_pCB_RemotePath->clear();
        m_pCB_RemotePath->addItems(paths);
    }

    m_config.destination.path = curPath;
    if (curPath != "" && !paths.contains(curPath)) {
        m_pCB_RemotePath->addItem(curPath);
    }

    if (m_config.destination.path == "") {
        m_config.destination.path = m_pCB_RemotePath->currentText();
    } else {
        m_pCB_RemotePath->setCurrentIndex(m_pCB_RemotePath->findText(m_config.destination.path));
    }

    //m_pCB_RemotePath->setEnabled(m_pCB_RemotePath->currentText() != "");
    //m_pPB_RemoteUser->setEnabled(m_pCB_RemotePath->currentText() != "");
    //m_pPB_DoBackup->setEnabled(m_pCB_RemotePath->currentText() != "");
}

//----------------------------------------------------------------------------
void QtdSync::slot_remoteSSHDestination(bool bBrowse)
{
    if (!m_config.destination.bRemote || !m_config.destination.bUseSSH) {
        m_config.destination.bUseSSH = true;
        m_config.destination.bRemote = true;
        if (!m_config.destination.bRemote) {
            m_config.destination.dest = "127.0.0.1";
        }
        m_config.destination.password = "";
        m_config.destination.path = "";
        slot_setRemoteUserAuth(true);
    }

    m_pW_Remote->show();
    m_pL_FreeSpace->hide();
    m_pGB_BackupInfo_Local->hide();
    m_pL_Destination->setText(tr("Host"));
    m_pLE_Destination->setReadOnly(false);
    m_pLE_Destination->setAcceptDrops(false);

    m_pCB_RemotePath->clear();
    m_pCB_RemotePath->hide();
    m_pLE_RemotePath->show();

    m_pLE_Destination->setText("ssh://"+m_config.destination.dest);

    slot_subfolderStateChanged(0, false);

    if (bBrowse) {
        m_pLE_RemotePath->setText(tr("Connecting to %1.").arg(m_config.destination.dest) + tr("Please wait..."));
        if (m_pDlg) {
            m_pDlg->setEnabled(false);
        }
        QtdDirTreeModel* pModel = 0L; 
        QUrl url("ssh://" + m_config.destination.dest);
        int nPort = url.port();
        QString host = url.host();

        //if (!m_config.destination.bUsePassword) {
        //    pModel = QtdTools::sshDirTreeModel(PEXEC("ssh"), host, nPort, m_config.destination.user, m_config.destination.password, "");
        //} else {
        //    pModel = QtdTools::sshDirTreeModel(PEXEC("sshpass"), host, nPort, m_config.destination.user, "", m_config.destination.password);
        //}

        if (pModel) {
            QDialog* pDlg = new QDialog(m_pDlg);
            Ui::QtdSyncSSHFolderDlg ui;
            ui.SetupUi(pDlg);

            ui.m_pTV_Folders->setModel(pModel);
            ui.m_pL_Info->setText("");

            if (pDlg->exec() == QDialog::Accepted) {
                QString preStr = m_config.destination.user + "@" + host;
                m_config.destination.path = pModel->selectedDir(ui.m_pTV_Folders).mid(preStr.length()+1);
            }
            delete pModel;
        } else {
            m_config.destination.path = "";
        }

        if (m_pDlg) {
            m_pDlg->setEnabled(true);
        }

    }

    if (m_config.destination.path == "") {
        m_pLE_RemotePath->setText(tr("Please enter the backup path here."));
        m_pLE_RemotePath->setFocus();
        m_pLE_RemotePath->selectAll();
    } else {
        m_pLE_RemotePath->setText(m_config.destination.path);
    }

    m_pPB_RemoteUser->setEnabled(true);
    m_pPB_DoBackup->setEnabled(true);
}


//----------------------------------------------------------------------------
void QtdSync::slot_setDestination()
{
    QMenu* pMenu = new QMenu(m_pDlg);
    pMenu->addAction(QIcon(":/images/icon_blue.png"), tr("Local path"), this, SLOT(slot_browseDestination()));
    pMenu->addSeparator();
    pMenu->addAction(QIcon(":/images/icon_yellow.png"), tr("Remote rsync path"), this, SLOT(slot_remoteDestination()));
    pMenu->addAction(QIcon(":/images/icon_green.png"), tr("Remote SSH path"), this, SLOT(slot_remoteSSHDestination()));

    pMenu->exec(QCursor::pos());
    delete pMenu;
}

//----------------------------------------------------------------------------
void QtdSync::slot_addTask()
{
    if (m_config.preProcessing.first == "" || m_config.postProcessing.first == "") {
        QMenu* pMenu = new QMenu(m_pDlg);
        pMenu->addAction(QIcon(":/images/icon_green.png"), tr("Folder"), this, SLOT(slot_addFolder()));
        pMenu->addSeparator();
        if (m_config.preProcessing.first == "") {
            pMenu->addAction(QIcon(":/images/prebackup.png"), tr("Preprocessing"), this, SLOT(slot_addPreScript()));
        }
        if (m_config.postProcessing.first == "") {
            pMenu->addAction(QIcon(":/images/postbackup.png"), tr("Postprocessing"), this, SLOT(slot_addPostScript()));
        }

        pMenu->exec(QCursor::pos());
        delete pMenu;
    } else {
        slot_addFolder();
    }
}


//----------------------------------------------------------------------------
void QtdSync::slot_browseKeyFile()
{
    QMenu* pMenu = new QMenu(0L);
    pMenu->addAction(tr("Set Password"));
    pMenu->addAction(tr("Browse for Keyfile"));

    QAction* pAct = pMenu->exec(QCursor::pos());
    if (pAct) {
        if (pAct->text() == tr("Set Password")) {
            m_config.destination.bUsePassword = true;
            uiRemoteUser.m_pL_Password->setText(tr("Password"));
            uiRemoteUser.m_pLE_Password->selectAll();
            uiRemoteUser.m_pLE_Password->setFocus();
        } else {
            QString file = QFileDialog::getOpenFileName(0L, tr("Select private key file"), "", tr("Keyfile (*.*)"));
            if (file != "") {
                file = browseForFolder(file, true);
            }
            m_config.destination.password = file;
            m_config.destination.bUsePassword = false;
            uiRemoteUser.m_pL_Password->setText(tr("Keyfile"));
            uiRemoteUser.m_pLE_Password->setText(evaluateString(m_config.destination.password));
        }
    }
    uiRemoteUser.m_pLE_Password->setEchoMode(m_config.destination.bUseSSH  && !m_config.destination.bUsePassword ? QLineEdit::Normal : QLineEdit::Password);
    uiRemoteUser.m_pLE_Password->setReadOnly(m_config.destination.bUseSSH && !m_config.destination.bUsePassword);
    uiRemoteUser.m_pChB_SavePassword->setText(m_config.destination.bUseSSH && !m_config.destination.bUsePassword ? tr("Save keyfile name to Backup set file") : tr("Save Password in Backup set file"));
}

//----------------------------------------------------------------------------
void QtdSync::slot_remoteSSHChecked(int state)
{
    if (state == Qt::Checked) {
        slot_browseKeyFile();
        m_config.destination.bUseSSH = true;
    } else {
        m_config.destination.bUseSSH = false;
        m_config.destination.password = "";
        uiRemoteUser.m_pLE_Password->setText("");
    }

    uiRemoteUser.m_pPB_BrowseKeyFile->setVisible(m_config.destination.bUseSSH);
    uiRemoteUser.m_pL_Password->setText(m_config.destination.bUseSSH && !m_config.destination.bUsePassword ? tr("Keyfile") : tr("Password"));
    uiRemoteUser.m_pLE_Password->setReadOnly(m_config.destination.bUseSSH && !m_config.destination.bUsePassword);
    uiRemoteUser.m_pLE_Password->setEchoMode(m_config.destination.bUseSSH  && !m_config.destination.bUsePassword ? QLineEdit::Normal : QLineEdit::Password);
    uiRemoteUser.m_pChB_SavePassword->setText(m_config.destination.bUseSSH && !m_config.destination.bUsePassword ? tr("Save keyfile name to Backup set file") : tr("Save Password in Backup set file"));
}

//----------------------------------------------------------------------------
void QtdSync::slot_setRemoteUserAuth(bool bSetServer)
{
    QDialog* pDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);

    uiRemoteUser.SetupUi(pDlg);
    bool bSetAborted = false;
    if (m_config.destination.password == "%%QTDSYNCING%%") {
        m_config.destination.password = "";
        uiRemoteUser.m_pChB_SavePassword->hide();
        bSetAborted = true;
    }
    uiRemoteUser.m_pLE_Name->setText(m_config.destination.user);
    uiRemoteUser.m_pLE_Password->setText(m_config.destination.bUseSSH && !m_config.destination.bUsePassword ? evaluateString(m_config.destination.password) : m_config.destination.password);
    uiRemoteUser.m_pChB_SavePassword->setChecked(m_config.destination.bStorePassWd);

    if (!m_config.destination.bUseSSH) {
        uiRemoteUser.m_pGB_UserAuth->setChecked(m_config.destination.bUserAuth);
    } else {
        uiRemoteUser.m_pGB_UserAuth->setChecked(true);
        uiRemoteUser.m_pGB_UserAuth->setCheckable(false);
    }

    if (!bSetServer) {
        uiRemoteUser.m_pLE_Server->hide();
    } else {
        uiRemoteUser.m_pLE_Server->show();
        uiRemoteUser.m_pLE_Server->setText(m_config.destination.dest);
        uiRemoteUser.m_pL_Server->hide();
    }

    uiRemoteUser.m_pChB_UseSSH->setVisible(false);//m_bSSHAvailable);
    if (!m_pDlg) {
        uiRemoteUser.m_pL_BackupSet->setText(m_config.name);
        uiRemoteUser.m_pL_Server->setText(m_config.destination.dest);
    } else {
        uiRemoteUser.m_pLine_Upper->hide();
        uiRemoteUser.m_pL_Info->hide();
        uiRemoteUser.m_pL_BackupSet->hide();
        if (!bSetServer) {
            uiRemoteUser.m_pL_Info2->hide();
        }
        uiRemoteUser.m_pL_Server->hide();
    }

    uiRemoteUser.m_pPB_BrowseKeyFile->setVisible(m_config.destination.bUseSSH);
    uiRemoteUser.m_pL_Password->setText(m_config.destination.bUseSSH && !m_config.destination.bUsePassword ? tr("Keyfile") : tr("Password"));
    uiRemoteUser.m_pChB_UseSSH->setChecked(m_config.destination.bUseSSH);
    uiRemoteUser.m_pLE_Password->setReadOnly(m_config.destination.bUseSSH && !m_config.destination.bUsePassword);
    if (m_config.destination.bUseSSH && !m_config.destination.bUsePassword) {
        uiRemoteUser.m_pLE_Password->setEchoMode(QLineEdit::Normal);
        uiRemoteUser.m_pChB_SavePassword->setText(tr("Save keyfile name to Backup set file"));
    }


    //connect(uiRemoteUser.m_pChB_UseSSH, SIGNAL(stateChanged(int)),  this, SLOT(slot_remoteSSHChecked(int)));
    connect(uiRemoteUser.m_pPB_BrowseKeyFile, SIGNAL(clicked()),    this, SLOT(slot_browseKeyFile()));

    qApp->processEvents();
    pDlg->adjustSize();

    if (pDlg->exec() == QDialog::Accepted) {
        m_config.destination.user = uiRemoteUser.m_pLE_Name->text();
        if (bSetServer) {
            m_config.destination.dest = uiRemoteUser.m_pLE_Server->text();
        }
        if (!m_config.destination.bUseSSH || m_config.destination.bUsePassword) {
            m_config.destination.password  = uiRemoteUser.m_pLE_Password->text();
        }
        m_config.destination.bStorePassWd = uiRemoteUser.m_pChB_SavePassword->isChecked();
        m_config.destination.bUserAuth  = m_config.destination.bUseSSH || uiRemoteUser.m_pGB_UserAuth->isChecked();
    } else if (bSetAborted) {
        m_config.destination.password = "%%QTDSYNCABORTED%%";
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_addPreScript(QString script)
{
    QString args = "";
    QString path = (script != "" && QFile(script).exists()) ? script : QFileDialog::getOpenFileName(0L, tr("Select Executable"), "", tr("Executables (*.exe *.com);;Scripts (*.bat *.cmd *.sh);;Any (*.*)"));
    path = browseForFolder(path, true);

    if (path != "") {
        editProcessingScript(true, path, args);
        if (path != "") {
            m_config.preProcessing.first = path;
            m_config.preProcessing.second = args;
            m_bRebuildTree = true;
        }
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_addPostScript(QString script)
{
    QString args = "";
    QString path = (script != "" && QFile(script).exists()) ? script : QFileDialog::getOpenFileName(0L, tr("Select Executable"), "", tr("Executables (*.exe *.com);;Scripts (*.bat *.cmd *.sh);;Any (*.*)"));
    path = browseForFolder(path, true);

    if (path != "") {
        editProcessingScript(true, path, args);
        if (path != "") {
            m_config.postProcessing.first = path;
            m_config.postProcessing.second = args;
            m_bRebuildTree = true;
        }
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_dataDropped(QStringList urls)
{
    foreach (QString url, urls) {
        url = url.trimmed();
#ifdef WIN32
        if (url.startsWith("/")) {
            url = url.mid(1);
        }
#endif
        if (url.startsWith("file://")) {
            url = url.mid(7);
        }
        slot_dataTextDropped(url);
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_dataTextDropped(QString text)
{
    foreach (QString dir, text.split("\n", QString::SkipEmptyParts)) {
        dir = dir.trimmed();

        if (dir.startsWith("file://")) {
            dir = dir.mid(7);
        }

        if (!QFileInfo(dir).isDir()) {
            dir = QFileInfo(dir).absolutePath();
        }
        if (!QFileInfo(dir).exists()) {
            return;
        }

        if (sender() == m_pLE_Destination) {
            slot_browseDestination(dir);
            break;
        } else {
            slot_addFolder(dir);
        }
    }
}

//----------------------------------------------------------------------------
bool QtdSync::slot_addFolder(QString folder)
{
    if (m_currentDlg == eMain) {
        Folder fold;
        fold.folder = browseForFolder(folder);
        fold.bCreateSubFolder = true;
        fold.rsyncExpert = "use-global-settings";
        if (m_config.type == eDifferential && !fold.rsyncExpert.contains("--delete-excluded")) {
            fold.rsyncExpert = m_config.globalRsyncOptions;
            fold.rsyncExpert += " --delete-excluded";
        }
        if (fold.folder != "") {
            fold.name = QDir(fold.folder).dirName();
            if (fold.name == "") {
#ifdef WIN32
                fold.name = fold.folder.at(0);
#else
                fold.name = "root";
#endif
            }

            fold.name = priv_QtdSync_getUniqueFolderName(m_config.folders, fold.name);

            m_config.folders.append(fold);
            m_bRebuildTree = true;
        } else {
            return false;
        }
    } else if (m_currentDlg == eServer) {
        VirtualDirConfig dir;
        dir.path = browseForFolder(folder);
        if (dir.path != "") {
            dir.name = QDir(dir.path).dirName();
            if (dir.name == "") {
#ifdef WIN32
                dir.name = dir.path.at(0);
#else
                dir.name = "root";
#endif
            }
            //editVirtualDir(dir);

            m_serverSettings.dirs.append(dir);
            updateServerDirectories(m_serverSettings.dirs.count()-1);
            saveConfig();
        } else {
            return false;
        }
    }

    return true;
}

//----------------------------------------------------------------------------
void QtdSync::slot_removeFolder()
{
    if (m_currentDlg == eMain) {
        QTreeWidgetItem* pCurIt = m_pTW_Folders->currentItem();
        if (!pCurIt) return;

        while (pCurIt->parent() != 0L) pCurIt = pCurIt->parent();

        if (!pCurIt) return;

        int nFolderCount = 0;
        for (int i = 0; i < m_pTW_Folders->topLevelItemCount(); i++) {
            if (m_pTW_Folders->topLevelItem(i) == pCurIt) {
                if (pCurIt->data(0, Qt::UserRole | 0x03).toInt() == 0) {
                    Folder fold = m_config.folders.at(nFolderCount);

                    if (QMessageBox::question(m_pDlg, tr("Remove Element"), tr("Remove the backup element <b>%1</b>?").arg(fold.name), QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes) {
                        m_config.folders.removeAt(nFolderCount);
                        //initDlg();
                        m_pTW_Folders->takeTopLevelItem(i);
                    }
                } else if (pCurIt->data(0, Qt::UserRole | 0x03).toInt() == 1) {
                    if (QMessageBox::question(m_pDlg, tr("Remove Element"), tr("Remove the backup set preprocessing step?"), QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes) {
                        m_config.preProcessing.first = "";
                        m_pTW_Folders->takeTopLevelItem(i);
                    }
                } else if (pCurIt->data(0, Qt::UserRole | 0x03).toInt() == 2) {
                    if (QMessageBox::question(m_pDlg, tr("Remove Element"), tr("Remove the backup set postprocessing step?"), QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes) {
                        m_config.postProcessing.first = "";
                        m_pTW_Folders->takeTopLevelItem(i);
                    }
                }
                break;
            }
            if (m_pTW_Folders->topLevelItem(i)->data(0, Qt::UserRole | 0x03).toInt() == 0) {
                nFolderCount++;
            }
        }
    } else if (m_currentDlg == eServer) {
        QTreeWidgetItem* pCurIt = uiServer.m_pTW_Folders->currentItem();
        if (!pCurIt) return;

        while (pCurIt->parent() != 0L) pCurIt = pCurIt->parent();

        if (!pCurIt) return;

        for (int i = 0; i < uiServer.m_pTW_Folders->topLevelItemCount(); i++) {
            if (uiServer.m_pTW_Folders->topLevelItem(i) == pCurIt) {
                VirtualDirConfig dir = m_serverSettings.dirs.at(i);

                if (QMessageBox::question(m_pDlg, tr("Remove Element"), tr("Remove the virtual directory <b>%1</b>?").arg(dir.name), QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes) {
                    m_serverSettings.dirs.removeAt(i);
                    uiServer.m_pTW_Folders->takeTopLevelItem(i);
                }
                break;
            }
        }
    }

}

//----------------------------------------------------------------------------
void QtdSync::slot_about()
{
    showDlg(eAbout);
}

//----------------------------------------------------------------------------
QString priv_getTimeStr(int type) 
{
    switch(type) {
        case 0: return "minute";
        case 1: return "hour";
        case 2: return "day";
        case 3: return "week";
        case 4: return "month";
        case 5: return "year";
    }
    return "day";
}

//----------------------------------------------------------------------------
int priv_getTimeId(QString type) 
{
    if (type == "minute") {
        return 0;
    } else if (type == "hour") {
        return 1;
    } else if (type == "day") {
        return 2;
    } else if (type == "week") {
        return 3;
    } else if (type == "month") {
        return 4;
    } else if (type == "year") {
        return 5;
    } else return 1;
}

//----------------------------------------------------------------------------
void QtdSync::slot_mailTest()
{
    QtdMail mail;
    mail.setSmtpServer(uiMail.m_pLE_Smtp->text(), uiMail.m_pChB_Encryption->isChecked());
    mail.setUserAuthenticationEnabled(uiMail.m_pGB_Authentication->isChecked(), uiMail.m_pLE_UserName->text(), uiMail.m_pLE_Password->text());
    
    QString address = uiMail.m_pLE_Mail->text();
    QRegExp regExp("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,4}$");
    if (!regExp.exactMatch(address)) {
        QMessageBox::information(0L, tr("E-Mail invalid?"), tr("The provided mail address<br><br><b>%1</b><br><br>seems to be invalid.").arg(address));
    } else {
        mail.setSender(address, m_productName + " v" + m_productVersion);
        mail.setReceiver(address, address);

        // set mail subject
        mail.setMailSubject(QString("[QtdSync Mail Test] %1").arg(m_config.name));

        //------------------------------------------------------------------------
        // compose body
        QString body;

        body += QString("Mail Test\r\n");
        body += "--------------------------------------------------------------\r\n";
        body += QString("Backup Set: %1 (%2)\r\n").arg(m_config.name).arg(m_config.fileName);
        body += QString("SMTP: " + uiMail.m_pLE_Smtp->text() + (uiMail.m_pChB_Encryption->isChecked() ? " (Encrypted)" : "")) + "\r\n";
        body += QString("User: " + (uiMail.m_pGB_Authentication->isChecked() ? uiMail.m_pLE_UserName->text() : "(" + tr("None") + ")")) + "\r\n";

        mail.setMailBody(body);
        QString infoMsg = tr("Mail delivered successfully.");
        bool bNoError = false;
        QMessageBox* pInfoBox = new QMessageBox();
        pInfoBox->setText(tr("Send Mail. Please wait..."));
        pInfoBox->setStandardButtons(0);
        pInfoBox->show();
        qApp->processEvents();

        switch (mail.sendMail()) {
            case QtdMail::eNoError:
                // leave message as it is
                bNoError = true;
                break;
            case QtdMail::eSendError_SmtpUndefined:
                infoMsg = tr("SMTP-Server undefined!");
                break;
            case QtdMail::eSendError_ReceiverUndefined:
                infoMsg = tr("Receiver address missing!");
                break;
            case QtdMail::eSendError_SenderUndefined:
                infoMsg = tr("Sender address missing!");
                break;
                /*
            case QtdMail::eSendError_WSAInitialization:
                infoMsg = tr("");
                break;
            case QtdMail::eSendError_SmtpNotFound:
                infoMsg = tr("");
                break;
            case QtdMail::eSendError_SocketInitialization:
                infoMsg = tr("");
                break;
                */
            case QtdMail::eSendError_ServerConnection:
                infoMsg = tr("Connecting to SMTP failed!");
                break;
                /*
            case QtdMail::eSendError_Initial:
                infoMsg = tr("");
                break;
                */
            case QtdMail::eSendError_Helo:
            case QtdMail::eSendError_MailFrom:
            case QtdMail::eSendError_MailTo:
            case QtdMail::eSendError_Data:
            case QtdMail::eSendError_Mail:
            case QtdMail::eSendError_Quit:
                infoMsg = tr("Protocol error!");
                break;
            case QtdMail::eSendError_UserName:
            case QtdMail::eSendError_Password:
                infoMsg = tr("The server denied access for the given user!");
                break;
            case QtdMail::eSendError_Unknown:
            default:
                infoMsg = tr("Unknown error!");
                break;
        }

        delete pInfoBox;

        if (bNoError) {
            QMessageBox::information(0L, tr("Mail Test"), infoMsg);
        } else {
            QMessageBox::critical(0L, tr("Mail Test"), infoMsg);
        }
    }

}

//----------------------------------------------------------------------------
void QtdSync::slot_mail()
{
    QDialog* aDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);
    uiMail.SetupUi(aDlg);

    uiMail.m_pLE_Smtp->setText(m_config.mail.smtp());
    uiMail.m_pLE_Mail->setText(m_config.mail.senderAddress());
    uiMail.m_pGB_Enable->setChecked(m_config.sendReport);
    uiMail.m_pChB_OnErrorOnly->setChecked(m_config.sendReportOnErrorOnly);
    uiMail.m_pChB_Encryption->setChecked(m_config.mail.encryptionEnabled());

    uiMail.m_pGB_Authentication->setChecked(m_config.mail.authenticationEnabled());
    if (m_config.mail.authenticationEnabled()) {
        uiMail.m_pLE_UserName->setText(m_config.mail.user());
        uiMail.m_pLE_Password->setText(m_config.mail.password());
    }

    connect(uiMail.m_pLE_Mail, SIGNAL(textEdited(const QString&)), this, SLOT(slot_mailEdited(const QString&)));
    connect(uiMail.m_pPB_SendTestMail, SIGNAL(clicked()), this, SLOT(slot_mailTest()));

mailagain:
    if (aDlg->exec() == QDialog::Accepted) {
        m_config.sendReport = uiMail.m_pGB_Enable->isChecked();
        m_config.sendReportOnErrorOnly = uiMail.m_pChB_OnErrorOnly->isChecked();
        m_config.mail.setSmtpServer(uiMail.m_pLE_Smtp->text(), uiMail.m_pChB_Encryption->isChecked());
        m_config.mail.setUserAuthenticationEnabled(uiMail.m_pGB_Authentication->isChecked(), uiMail.m_pLE_UserName->text(), uiMail.m_pLE_Password->text());
    
        QString address = uiMail.m_pLE_Mail->text();
        QRegExp regExp("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,4}$");
        if (m_config.sendReport && !regExp.exactMatch(address)) {
            if (QMessageBox::question(0L, tr("E-Mail invalid?"), tr("The provided mail address<br><br><b>%1</b><br><br>seems to be invalid. Do you want to recheck it?").arg(address),
                QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
                goto mailagain;
            }
        }
        m_config.mail.setSender(address);
        m_config.mail.setReceiver(address);
    }
    delete aDlg;
}

//----------------------------------------------------------------------------
void QtdSync::slot_schedule()
{
    QDialog* aDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);
    Ui::QtdSyncSchedule sUi;
    sUi.SetupUi(aDlg);

    sUi.m_pL_BackupSet->setText(m_config.name);
    sUi.m_pCB_ConditionType->setCurrentIndex(priv_getTimeId(m_config.schedule.conditionType));
    sUi.m_pGB_Retry->setChecked(m_config.schedule.bRetry);
    sUi.m_pCB_RetryType->setCurrentIndex(priv_getTimeId(m_config.schedule.retryType));
    sUi.m_pCB_EveryType->setCurrentIndex(priv_getTimeId(m_config.schedule.everyType));
    sUi.m_pGB_Plugin->setChecked(m_config.schedule.bOnPlugin);
    sUi.m_pGB_Schedule->setChecked(m_config.schedule.bOnTime);
    sUi.m_pSB_ConditionCount->setValue(m_config.schedule.nConditionCount);
    sUi.m_pSB_RetryCount->setValue(m_config.schedule.nRetryNumber);
    sUi.m_pSB_EveryCount->setValue(m_config.schedule.nEveryCount);
    sUi.m_pStartTime->setDateTime(m_config.schedule.startTime);
    sUi.m_pGB_Silent->setChecked(m_config.schedule.bSilent);
    sUi.m_pChB_MarkAsValid->setChecked(m_config.schedule.bMarkFailedAsValid);
    sUi.m_pCB_ProcessPrio->setCurrentIndex(m_config.scheduledPrio);

#ifdef WIN32
    QSettings* pSettings = new QSettings(QSettings::UserScope, "Microsoft", "Windows");
    bool bHaveRegistryEntry = pSettings->contains("CurrentVersion/Run/QtdSync");
#else
    bool bHaveRegistryEntry = QFile("/etc/rc5.d/S99qtdsyncmonitor").exists();
    sUi.m_pW_ProcessPriority->hide();
#endif
    bool bNeedRegistryEntry = false;
    if (aDlg->exec() == QDialog::Accepted) {
        m_config.schedule.conditionType     = priv_getTimeStr(sUi.m_pCB_ConditionType->currentIndex());
        m_config.schedule.retryType         = priv_getTimeStr(sUi.m_pCB_RetryType->currentIndex());
        m_config.schedule.everyType         = priv_getTimeStr(sUi.m_pCB_EveryType->currentIndex());
        m_config.schedule.bOnPlugin         = sUi.m_pGB_Plugin->isChecked();
        m_config.schedule.bOnStartup        = m_config.schedule.bOnPlugin;
        m_config.schedule.bOnTime           = sUi.m_pGB_Schedule->isChecked();
        m_config.schedule.nConditionCount   = sUi.m_pSB_ConditionCount->value();
        m_config.schedule.nRetryNumber      = sUi.m_pSB_RetryCount->value();
        m_config.schedule.nEveryCount       = sUi.m_pSB_EveryCount->value();
        m_config.schedule.startTime         = sUi.m_pStartTime->dateTime();
        m_config.schedule.bSilent           = sUi.m_pGB_Silent->isChecked();
        m_config.schedule.bMarkFailedAsValid = sUi.m_pChB_MarkAsValid->isChecked();
        m_config.scheduledPrio              = sUi.m_pCB_ProcessPrio->currentIndex();
        m_config.schedule.bRetry            = sUi.m_pGB_Retry->isChecked();

        bNeedRegistryEntry = m_config.schedule.bOnPlugin || m_config.schedule.bOnTime;
        m_config.unSaved = true;
        saveQtd();
    }

    if (bNeedRegistryEntry && (!bHaveRegistryEntry || !isMonitorRunning())) {
        if (!bHaveRegistryEntry) {
#ifdef WIN32
            pSettings->setValue("CurrentVersion/Run/QtdSync", QString("\"%1\\QtdSyncMonitor.exe\"").arg(qApp->applicationDirPath().replace("/", "\\")));
#else
            ShellExecuteA(NULL, "runas", qApp->applicationFilePath().toLatin1().data(), "--enableStartupMonitor", "", 0);
#endif

        }
#ifdef WIN32
        QMessageBox::information(m_pDlg, tr("Backup schedule"), tr("You have activated backup scheduling.<br><br>To use that feature the <b>QtdSyncMonitor</b> must be started now and on Windows startup.<br><br>Therefor a registry entry was necessary. If you feel uncomfortable with that you can disable it under <b>Settings</b>."));
#else
        QMessageBox::information(m_pDlg, tr("Backup schedule"), tr("You have activated backup scheduling.<br><br>To use that feature the <b>QtdSyncMonitor</b> must be started now and on Linux startup. Therefor it is necessary to grant QtdSync root previleges to setup the needed startup script."));
#endif
        if (!isMonitorRunning()) {
            ShellExecuteA(NULL, "open", qApp->applicationFilePath().toLatin1().data(), "-m", "", SW_SHOW);
        }

    }
#ifdef WIN32
    delete pSettings;
#endif
    delete aDlg;
}

//----------------------------------------------------------------------------
void QtdSync::slot_settings()
{
    QDialog* aDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);
    uiSettings.SetupUi(aDlg);
    QSettings* pSettings = 0L;

#ifdef WIN32
    pSettings = new QSettings(QSettings::UserScope, "Microsoft", "Windows");

    uiSettings.m_pChB_Monitor->setChecked(pSettings->contains("CurrentVersion/Run/QtdSync"));
    uiSettings.m_pChB_Server->setChecked(pSettings->contains("CurrentVersion/Run/QtdSyncServer"));
    uiSettings.m_pChB_DriverLetters->setChecked(m_settings.monitor.bDriveLetters);
    uiSettings.m_pLE_ProxyHost->setText(m_settings.proxyHost);
    uiSettings.m_pSB_ProxyPort->setValue(m_settings.proxyPort);
    uiSettings.m_pChB_ProcessPrio->setChecked(m_settings.setProcessPrio);
    uiSettings.m_pCB_ProcessPrio->setCurrentIndex(m_settings.processPrio);
    uiSettings.m_pCB_ProcessPrio->setEnabled(m_settings.setProcessPrio);

    int i = 1;
    uiSettings.m_pCB_CheckDrives->setAutoBuildDisplayText(false);
    uiSettings.m_pCB_CheckDrives->setDisplayText(tr("Select drive types..."));
    foreach (QString driveType, QString(tr("Floppy drives")+"|"+tr("Local hard drives")+"|"+tr("Removable drives (e.g. USB)")+"|"+tr("Network drives")+"|"+tr("CD/DVD drives")+"|"+tr("RamDisks")).split("|")) {
        uiSettings.m_pCB_CheckDrives->addItem(driveType, m_settings.monitor.driveTypes.contains((QtdTools::DriveType)i));
        i++;
    }
#else
    bool bHaveMonitor = QFile("/etc/rc5.d/S99qtdsyncmonitor").exists();
    uiSettings.m_pChB_Monitor->setChecked(false);
    uiSettings.m_pChB_Server->setChecked(false);
    uiSettings.m_pGB_Update->setChecked(false);
    uiSettings.m_pChB_DriverLetters->setChecked(false);
    uiSettings.m_pChB_DriverLetters->hide();
    uiSettings.m_pFrame_DriveLetters->hide();
    uiSettings.m_pGB_WindowsStartup->setTitle(tr("Linux Startup"));
    uiSettings.m_pChB_Monitor->setChecked(bHaveMonitor);
    uiSettings.m_pChB_Server->hide();
    uiSettings.m_pW_ProcessPriority->hide();
#endif
    uiSettings.m_pGB_Update->setChecked(m_settings.doUpdate);

#ifndef QTD_HAVE_UPDATECLIENT
    uiSettings.m_pGB_Update->hide();
#endif
    uiSettings.m_pChB_FolderBinding->setChecked(m_settings.showFolderBindingOptions);
    uiSettings.m_pChB_RsyncExpertSettings->setChecked(m_settings.showRsyncExpertSettings);

    uiSettings.m_pCB_Language->clear();
    uiSettings.m_pCB_Language->addItem(tr("<Auto>"));
    QList<QLocale::Language> langs = availableTranslations();
    int nCurEntry = 0;
    for (int i = 0; i < langs.count(); i++) {
        QString language = QLocale::languageToString(langs.at(i));
        uiSettings.m_pCB_Language->addItem(language);
        uiSettings.m_pCB_Language->setItemData(i+1, (int)langs.at(i));

        if (m_settings.language == langs.at(i)) {
            nCurEntry = i+1;
        }
    }
    uiSettings.m_pCB_Language->setCurrentIndex(nCurEntry);

    qApp->processEvents();
    aDlg->adjustSize();

    if (aDlg->exec() == QDialog::Accepted) {
        m_settings.proxyHost = uiSettings.m_pLE_ProxyHost->text();
        m_settings.proxyPort = uiSettings.m_pSB_ProxyPort->value();
        m_settings.doUpdate = uiSettings.m_pGB_Update->isChecked();
        m_settings.monitor.bDriveLetters = uiSettings.m_pChB_DriverLetters->isChecked();
        m_settings.showFolderBindingOptions = uiSettings.m_pChB_FolderBinding->isChecked();
        m_settings.showRsyncExpertSettings = uiSettings.m_pChB_RsyncExpertSettings->isChecked();
#ifdef WIN32
        m_settings.setProcessPrio = uiSettings.m_pChB_ProcessPrio->isChecked();
        m_settings.processPrio = (QtdTools::ProcessPriority)uiSettings.m_pCB_ProcessPrio->currentIndex();
#endif

        if (m_currentDlg == eMain) {
            m_pGB_RsyncExpert->setVisible(m_settings.showRsyncExpertSettings);
            if (m_settings.showRsyncExpertSettings) {
                m_pLE_RsyncOptions->setText(m_config.globalRsyncOptions);
            }
        }
        
        nCurEntry = uiSettings.m_pCB_Language->currentIndex();
        if (!nCurEntry) {
            if (QLocale::system().language() != m_settings.language && m_settings.language > 0) {
                QMessageBox::information(aDlg, tr("Language changed"), tr("You changed the language settings. Please restart QtdSync."));
            }
            m_settings.language = (QLocale::Language)0;
        } else {
            int nLang = uiSettings.m_pCB_Language->itemData(nCurEntry).toInt();
            if (m_settings.language != (QLocale::Language)nLang) {
                QMessageBox::information(aDlg, tr("Language changed"), tr("You changed the language settings. Please restart QtdSync."));
            }

            m_settings.language = (QLocale::Language)nLang;
        }

        m_settings.monitor.driveTypes.clear();
#ifdef WIN32
        for (int i = 1; i <= uiSettings.m_pCB_CheckDrives->count(); i++) {
            if (uiSettings.m_pCB_CheckDrives->isChecked(i-1)) {
                m_settings.monitor.driveTypes << (QtdTools::DriveType)i;
            }
        }
#endif

#ifdef WIN32
        if (uiSettings.m_pChB_Monitor->isChecked()) {
            pSettings->setValue("CurrentVersion/Run/QtdSync", QString("\"%1\\QtdSyncMonitor.exe\"").arg(qApp->applicationDirPath().replace("/", "\\")));
        } else {
            pSettings->setValue("CurrentVersion/Run/QtdSync", "");
            pSettings->remove("CurrentVersion/Run/QtdSync");
        }

        if (uiSettings.m_pChB_Server->isChecked()) {
            pSettings->setValue("CurrentVersion/Run/QtdSyncServer", QString("\"%1\\QtdSyncServer.exe\" --start").arg(qApp->applicationDirPath().replace("/", "\\")));
        } else {
            pSettings->setValue("CurrentVersion/Run/QtdSyncServer", "");
            pSettings->remove("CurrentVersion/Run/QtdSyncServer");
        }
#else
        if (uiSettings.m_pChB_Monitor->isChecked()) {
            if (!bHaveMonitor) {
                ShellExecuteA(NULL, "runas", qApp->applicationFilePath().toLatin1().data(), "--enableStartupMonitor", "", 0);
            }
        } else if (bHaveMonitor) {
            ShellExecuteA(NULL, "runas", qApp->applicationFilePath().toLatin1().data(), "--disableStartupMonitor", "", 0);
        }
#endif
        saveConfig();
    }
    delete aDlg;
    if (pSettings) delete pSettings;


}

//----------------------------------------------------------------------------
void QtdSync::slot_showStartup()
{
    if (m_timer) m_timer->stop();

    m_pDlg->hide();
    m_pDlg->deleteLater();
    m_pDlg = 0L;

    showDlg(eStartup);
    fillStartupDlg(m_settings.knownQtd.keys());

    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer,                SIGNAL(timeout()),           this, SLOT(slot_checkModified()));
    }
    m_timer->start(2000);
}

//----------------------------------------------------------------------------
void QtdSync::slot_accept()
{
    bool bAbort = false;
    slot_beforeRestart(bAbort);
    if (m_currentDlg == eMain) {
        if (!m_serverSettings.bSelf) {
            QTimer::singleShot(200, this, SLOT(slot_showStartup()));
        } else {
            quit();
        }
    } else {
        quit();
    }
}


//----------------------------------------------------------------------------
void QtdSync::slot_cancel()
{
    if (m_config.unSaved) {
        if (QMessageBox::question(0L, tr("QtdSync Cancel"), tr("Do you really wanna quit without saving?"), QMessageBox::Yes |  QMessageBox::No) == QMessageBox::No) {
           return;
        }
    }
    if (m_currentDlg == eMain) {
        if (!m_serverSettings.bSelf) {
            QTimer::singleShot(200, this, SLOT(slot_showStartup()));
        } else {
            quit();
        }
    } else {
        quit();
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_globalRsyncReset()
{
    m_config.globalRsyncOptions = QTD_DEF_RSYNC_OPTIONS;
    m_pLE_RsyncOptions->setText(m_config.globalRsyncOptions);
}

//----------------------------------------------------------------------------
void QtdSync::slot_globalRsyncChanged(QString text)
{
    m_config.globalRsyncOptions = text;
}

//----------------------------------------------------------------------------
void QtdSync::slot_beforeRestart(bool& bAbort)
{
    if (m_pUpdateDlg) {
        delete m_pUpdateDlg;
        m_pUpdateDlg = 0L;

        QMessageBox* pBox = new QMessageBox(QMessageBox::Question, tr("QtdSync Update"),
                                            tr("To apply the update QtdSync needs to be restarted now."));
        QPushButton* pPb = pBox->addButton(tr("Restart now"), QMessageBox::YesRole);
        pBox->setDefaultButton(pPb);
        pBox->addButton(tr("Abort update"), QMessageBox::NoRole);

        pBox->exec();
        bAbort = !(pBox->clickedButton() == pPb);
        delete pBox;
   }

    if (!bAbort) {
        if (m_pTrayMenu) {
            delete m_pTrayMenu;
        }
        if (m_pTrayIcon) {
            m_pTrayIcon->hide();
            delete m_pTrayIcon;
        }
        saveQtd();
    } else {
        if (m_pTrayIsActive) {
            m_timer->start(5000);
        }
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_elementSelected(QTreeWidgetItem* pItem, int)
{
    if (m_currentDlg == eStartup) {
        if (!pItem || !(pItem->flags() & Qt::ItemIsUserCheckable)) {
            uiStartup.m_pPB_RemoveBackup->setEnabled(pItem != 0L);
            uiStartup.m_pPB_EditBackup->setEnabled(pItem == 0L);
            if (pItem) {
                uiStartup.m_pPB_RemoveBackup->setText(qApp->translate("QtdSyncStartup", QT_TRANSLATE_NOOP("QtdSyncStartup", "Remove selected Backup set from list")));
            }
        } else {
            uiStartup.m_pPB_RemoveBackup->setText(qApp->translate("QtdSyncStartup", QT_TRANSLATE_NOOP("QtdSyncStartup", "Delete selected Backup set")));
            uiStartup.m_pPB_EditBackup->setEnabled(true);
            uiStartup.m_pPB_RemoveBackup->setEnabled(true);
        }

        int count = 0;
        for (int i = 0; i < uiStartup.m_pTW_Sets->topLevelItemCount(); i++) {
            pItem = uiStartup.m_pTW_Sets->topLevelItem(i);
            if (pItem->flags() & Qt::ItemIsUserCheckable && pItem->checkState(0) == Qt::Checked) {
                count++;   
            }
        }
        uiStartup.m_pPB_DoBackup->setEnabled(count > 0);
    } else {
        if (!pItem) return;
        if (pItem->data(0, Qt::UserRole | 0x01).toInt() == -1) return;

        Qt::ItemFlags flags = pItem->flags();
        if (!(flags & Qt::ItemIsUserCheckable)) return;
        Folder fold = m_config.folders.at(pItem->data(0, Qt::UserRole | 0x01).toInt());
        QString relativePath = "/" + QDir(evaluateString(fold.folder)).relativeFilePath(pItem->data(0, Qt::UserRole).toString());
        if (pItem->checkState(0) != Qt::Unchecked) {
            fold.filesExcluded.removeAll(relativePath);
            if (!pItem->data(0, Qt::UserRole | 0x02).toBool()) {
                m_pCB_Name->setEnabled(false);
                fillTreeElement(pItem, false, 1);
                m_pCB_Name->setEnabled(true);
                pItem->setExpanded(true);
            }
        } else {
            clearTreeElement(pItem);
            if (!fold.filesExcluded.contains(relativePath)) {
                fold.filesExcluded << relativePath;
            }
        }
        m_config.folders.replace(pItem->data(0, Qt::UserRole | 0x01).toInt(), fold);
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_doRestore()
{
    QTreeWidgetItem* pCurIt = m_pTW_Folders->currentItem();
    if (!pCurIt) return;
    while (pCurIt->parent() != 0L) pCurIt = pCurIt->parent();

    if (!pCurIt) return;
    int id = m_pTW_Folders->indexOfTopLevelItem(pCurIt);
    if (id < 0 || id >= m_pTW_Folders->topLevelItemCount()) return;

    if (m_currentDlg == eMain) {
        saveQtd();

        QDialog* pDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);
        Ui::QtdSyncScheduledBackup rUi;

        rUi.SetupUi(pDlg);
        rUi.m_pL_Caption->setText(tr("QtdSync Restore"));
        rUi.m_pL_Info->setText(tr("Restoring a backup set will <b>overwrite</b> and possibly<br><b>delete</b> existing files in the destination folder.<br><br>Do you want to"));

        rUi.m_pPB_DoBackup->setText(tr("Use the defined backup source as destination"));
        rUi.m_pPB_AbortBackup->setText(tr("Define another destination folder"));
        rUi.m_pPB_AbortBackup->setIcon(QIcon(":/images/edit.png"));

        
        qApp->processEvents();
        pDlg->adjustSize();

        QMap<QString, QString> config;
        for (int i = 0; i < m_config.folders.count(); i++) {
            if (i != id) config[QString("exclude=%1").arg(m_config.folders.at(i).name)] = "true";
        }   

        if (pDlg->exec() != QDialog::Accepted) {
            QString backupFolder = QFileDialog::getExistingDirectory(m_pDlg, tr("QtdSync backup destination for \"%1\"").arg(m_config.name), "");
            if (backupFolder == "") {
                delete pDlg;
                return;
            }
            config["backuplocation"]    = backupFolder;
            config["override"]          = "true";
        }
        config["restore"] = "true";
        doBackup(QStringList(m_config.fileName), config);
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_doBackup()
{
    if (m_currentDlg == eMain) {
        saveQtd();
        QMap<QString, QString> config;
        doBackup(QStringList(), config);
    } else {
        QStringList backupSets;
        for (int i = 0; i < uiStartup.m_pTW_Sets->topLevelItemCount(); i++) {
            QTreeWidgetItem* pItem = uiStartup.m_pTW_Sets->topLevelItem(i);
            if (pItem->checkState(0) == Qt::Checked) {
                backupSets << pItem->data(0, Qt::UserRole).toString();
            }
        }
        doBackup(backupSets, QMap<QString, QString>());
    }
}

//----------------------------------------------------------------------------
QString QtdSync::lastBackupString(QDateTime dt)
{
    QDateTime zero;

    if (!dt.isValid()) {
        return tr("Unknown");
    }

    int nSecs = dt.secsTo(QDateTime::currentDateTime());
    int nDays, nHours, nMins;

    nSecs -= (nDays     = nSecs / (60*60*24)) * (60*60*24);
    nSecs -= (nHours    = nSecs / (60*60))    * (60*60);
    nSecs -= (nMins     = nSecs / (60))       * (60);

    QString retTime = tr("%1%2%3 ago")
        .arg(nDays  > 0 ? tr("%1 days").arg(nDays) + (nHours > 0 ? " " + tr("and") + " " : "") : "")
        .arg(nHours > 0 ? tr("%1 hours").arg(nHours) + (nMins > 0 && nDays == 0 ? " " + tr("and") + " " : "") : "")
        .arg(nDays == 0 ? tr("%1 minutes").arg(nMins) : "");

    return retTime;
}



//----------------------------------------------------------------------------
void QtdSync::slot_checkModified()
{
    if (m_currentDlg == eMain) {

        slot_checkForUpdate();

        if (m_pTmpDlg) {
            delete m_pTmpDlg;
            m_pTmpDlg = 0L;
        }
        m_pDlg->setWindowTitle(m_productName + " v" + m_productVersion + " - " + m_config.fileName + QString("%1").arg((m_config.unSaved = qtdHasChanged()) ? "*" : ""));
        
        m_geometry = m_pDlg->saveGeometry();
 
        if (m_config.lastBackup.isValid()) {
            if (m_config.lastBackup.addDays(3) < QDateTime::currentDateTime()) {
                m_pL_LastBackup->setStyleSheet("color: red");
            } else if (m_config.lastBackup.addDays(1) < QDateTime::currentDateTime()) {
                m_pL_LastBackup->setStyleSheet("color: orange");
            } else {
                m_pL_LastBackup->setStyleSheet("color: green");
            }
            m_pL_LastBackup->setText(lastBackupString(m_config.lastBackup));
        } else {
            m_pL_LastBackup->setStyleSheet("color: red");
            m_pL_LastBackup->setText(tr("Unknown"));
        }
        if (!m_settings.knownQtd.keys().contains(QFileInfo(m_config.fileName).absoluteFilePath()) && QFile(m_config.fileName).exists()) {
            addToKnownQtd(QFileInfo(m_config.fileName).absoluteFilePath());
            saveConfig();
        }

        if (m_bRebuildTree) {
            m_bRebuildTree = false;
            initDlg();
        }   

    } else if (m_currentDlg == eStartup) {
        bool bRebuild = false;
        QPair<QStringList, QStringList> remAdd = validateKnownQtd();

        foreach(QString rem, m_currentRemoved) {
            if (!remAdd.first.contains(rem)) {
                bRebuild = true;
            }
        }

        foreach(QString rem, remAdd.first) {
            if (!m_currentRemoved.contains(rem)) {
                bRebuild = true;
            }
        }
        m_currentRemoved = remAdd.first;

        if (bRebuild) {
            fillStartupDlg(m_settings.knownQtd.keys());
        }
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_checkForUpdate()
{
#ifdef QTD_HAVE_UPDATECLIENT
    if (m_updateClient && !m_applicationConfig.contains("noupdate") && m_settings.doUpdate && 
        (!m_updateClient->lastUpdateCheck().isValid() || m_updateClient->lastUpdateCheck() < QDateTime::currentDateTime().addDays(-1))) {
        slot_forcedCheckForUpdate();
    }
#endif
}

//----------------------------------------------------------------------------
void QtdSync::slot_forcedCheckForUpdate()
{
#ifdef QTD_HAVE_UPDATECLIENT
    if (m_updateClient) {
        if (QtdTools::getAllProcesses(QFileInfo(qApp->applicationFilePath()).fileName()).count() < 2) {
            m_updateClient->checkForUpdate();
        }
    }
#endif
}

//----------------------------------------------------------------------------
void QtdSync::slot_itemExpanded(QTreeWidgetItem* pItem)
{
    if (!pItem || pItem->data(0, Qt::UserRole | 0x02).toBool() || m_nFillingTreeDepth != 0) return;

    if (m_currentDlg == eMain) m_pCB_Name->setEnabled(false);
    fillTreeElement(pItem, false, 1);
    if (m_currentDlg == eMain) m_pCB_Name->setEnabled(true);
}

//----------------------------------------------------------------------------
void QtdSync::slot_updateAvailable(const QStringList& description, const QString& version)
{
    if (m_currentDlg == eMain) {
        m_pUpdate->show();
        m_pUpdate->setText(QString("v%1").arg(version));
        m_updateInfo.clear();
        m_updateInfo << description;
#ifndef QT_NO_TOOLTIP
        if (m_updateInfo.count() > 0) {
            m_pUpdate->setToolTip("- " + m_updateInfo.join("\n- "));
        }
#endif // QT_NO_TOOLTIP
        m_updateVersion = version;
#ifdef QTD_HAVE_UPDATECLIENT
        if (m_updateClient) {
            m_updateClient->setRestartArgs(QStringList(m_config.fileName));
        }
#endif
    } else if (m_currentDlg == eTray && m_pTrayIcon) {
        m_updateInfo.clear();
        m_updateVersion = version;
        m_updateInfo << description;
        QString toolTip = m_updateInfo.count() > 0 ? "- " + m_updateInfo.join("\n- ")+"\n\n" : "";
        toolTip += tr("Click here to run update.");
        m_pTrayIcon->showMessage(tr("Update to v%1").arg(version), toolTip, QSystemTrayIcon::Information);
#ifdef QTD_HAVE_UPDATECLIENT
        if (m_updateClient) {
            m_updateClient->setRestartArgs(QStringList("-m"));
        }
#endif
        connect(m_pTrayIcon, SIGNAL(messageClicked()), this, SLOT(slot_update()));
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_update()
{
    if (m_pUpdateDlg != 0L) {
        return;
    }

    if (m_pTrayIsActive) {
        m_timer->stop();
        //if (m_pTrayMenu) {
        //    delete m_pTrayMenu;
        //}
        //if (m_pTrayIcon) {
        //    delete m_pTrayIcon;
        //}   
    }

    m_pUpdateDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);
    uiUpdate.SetupUi(m_pUpdateDlg);

    uiUpdate.m_pUpdateProgress->hide();
    uiUpdate.m_pUpdateLocation->hide();
    uiUpdate.m_pUpdateInfo->setText("<ul><li>" + m_updateInfo.join("</li><li>") + "</li></ul>");
    uiUpdate.m_pUpdateHeader->setText(m_productName + " v"+ m_updateVersion);

    qApp->processEvents();
    m_pUpdateDlg->adjustSize();

    if (m_pUpdateDlg->exec() == QDialog::Accepted) {
        uiUpdate.m_pUpdateProgress->show();
        uiUpdate.m_pPB_Update->hide();
        uiUpdate.m_pPB_Abort->hide();
        uiUpdate.m_pUpdateLocation->show();

        qApp->processEvents();
        m_pUpdateDlg->adjustSize();

        m_pUpdateDlg->show();

#ifdef QTD_HAVE_UPDATECLIENT
        if (m_updateClient) {
            connect(m_updateClient, SIGNAL(updateProgress(QString, int)), this, SLOT(slot_updateProgress(QString,int)));
            m_updateClient->doUpdate();
        }
#endif
    } else if (m_pUpdateDlg) {
        delete m_pUpdateDlg;
        m_pUpdateDlg = 0L;
        if (m_pTrayIsActive) {
            m_timer->start(5000);
        }
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_updateProgress(QString file, int percent)
{
    uiUpdate.m_pUpdateLocation->setText(QFileInfo(file).fileName());
    uiUpdate.m_pUpdateProgress->setValue(percent);
}

//----------------------------------------------------------------------------
void QtdSync::slot_updateFinished(bool bSuccess, QStringList updateLog)
{
    if (m_pUpdateDlg) {
        delete m_pUpdateDlg;
        m_pUpdateDlg = 0L;
    }

    if (!bSuccess) {
        QString last("");
        if (updateLog.count() > 0) last = updateLog.last();
        QMessageBox::warning(m_pDlg, tr("Update failed"), tr("An error occured during update.<br><br><b>%1</b>").arg(last));
    }
}

//----------------------------------------------------------------------------
void QtdSync::slot_authenticationRequired(const QString &hostname, quint16, QAuthenticator* auth)
{
    QDialog* aDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);
    Ui::QtdSyncAuth aUi;

    aUi.SetupUi(aDlg);
    aUi.m_pL_Info->setText(tr("The server for the software update needs authentication."));
    aUi.m_pL_ServerInfo->setText(QString("<b>%1</b> @ <b>%2</b>").arg(auth->realm()).arg(hostname));
    if (aDlg->exec() == QDialog::Accepted) {
        auth->setUser(aUi.m_pLE_Name->text());
        auth->setPassword(aUi.m_pLE_Password->text());
    }
    delete aDlg;
}

//----------------------------------------------------------------------------
void QtdSync::slot_proxyAuthenticationRequired(const QNetworkProxy& proxy, QAuthenticator* auth)
{
    QDialog* aDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);
    Ui::QtdSyncAuth aUi;

    aUi.SetupUi(aDlg);
    aUi.m_pL_Info->setText(tr("The proxy server for the software update needs authentication."));
    aUi.m_pL_ServerInfo->setText(QString("<b>%1</b>").arg(proxy.hostName()));
    if (aDlg->exec() == QDialog::Accepted) {
        auth->setUser(m_settings.proxyUser = aUi.m_pLE_Name->text());
        auth->setPassword(m_settings.proxyPassWd = aUi.m_pLE_Password->text());

#ifdef QTD_HAVE_UPDATECLIENT
        if (m_updateClient) {
            m_updateClient->setProxy(m_settings.proxyHost, m_settings.proxyPort, m_settings.proxyUser, m_settings.proxyPassWd);
        }
#endif
    }
    delete aDlg;

}

//----------------------------------------------------------------------------
void QtdSync::slot_monitorOpenQtdSync()
{
    QString fileName = qApp->applicationFilePath();
    ShellExecuteA(NULL, "open", fileName.toLatin1().data(), "-n", "", SW_SHOW);
}

//----------------------------------------------------------------------------
void QtdSync::slot_monitorOpenQtdSyncServer()
{
    QString fileName = qApp->applicationFilePath();
#ifdef WIN32
    ShellExecuteA(NULL, "open", fileName.toLatin1().data(), "--server-config", "", SW_SHOW);
#endif
}

//----------------------------------------------------------------------------
void QtdSync::slot_monitorCheckedScheduled()
{
    if (m_pTrayIsActive) {
        m_settings.monitor.bActive = m_pTrayIsActive->isChecked();
        bool bServerIsRunning = isServerRunning();
        if (!m_settings.monitor.bActive && m_timer->isActive()) {
            m_timer->stop();
        } else if (!m_timer->isActive()) {
            m_timer->start(5000);
        }
        QString iconFile = m_settings.monitor.bActive ? ":/images/schedule" : ":/images/schedule_off";
        if (bServerIsRunning) {
            iconFile += "_server";
        }
        m_pTrayIcon->setIcon(QIcon(iconFile + ".png"));
        QString toolTip = "QtdSync Monitor: " + (m_settings.monitor.bActive ? tr("Active"):tr("Not Active"));
#ifdef WIN32
        toolTip += "\nQtdSync Server: " + (bServerIsRunning ? tr("Running.") : tr("Stopped!"));
#endif
        m_pTrayIcon->setToolTip(toolTip);
    }
}

//----------------------------------------------------------------------------
bool QtdSync::askForBackup(QString name, ScheduleType type)
{
    m_pUpdateDlg = new QDialog(m_pDlg, Qt::FramelessWindowHint | Qt::Dialog | Qt::CustomizeWindowHint);
    Ui::QtdSyncScheduledBackup bUi;
    bUi.SetupUi(m_pUpdateDlg);

    if (type == eOnPlugin) {
        bUi.m_pL_Info->setText(tr("You have plugged in the backup set<br><b>%1</b>").arg(name) + "<br><br>" + tr("Do backup now?"));
    } else if (type == eOnStartup) {
        bUi.m_pL_Info->setText(tr("The backup set<br><b>%1</b><br>is scheduled for startup backup.").arg(name) + "<br><br>" + tr("Do backup now?"));
    } else if (type == eOnShutdown) {
        bUi.m_pL_Info->setText(tr("The backup set<br><b>%1</b><br>is scheduled for shutdown backup.").arg(name) + "<br><br>" + tr("Do backup now?"));
    } else if (type == eOnTime) {
        bUi.m_pL_Info->setText(tr("The backup set<br><b>%1</b><br>is scheduled for now.").arg(name) + "<br><br>" + tr("Do backup now?"));
    }

    qApp->processEvents();
    m_pUpdateDlg->adjustSize();

    int ret = m_pUpdateDlg->exec();
    delete m_pUpdateDlg;
    m_pUpdateDlg = 0L;

    return ret == QDialog::Accepted;
}

//----------------------------------------------------------------------------
bool QtdSync::doSchedulePlugin(QString fileName, QString uuid, ScheduleType type, bool& bFailed)
{
    bool bBackupDone = false;
    bFailed = false;

    if (type == eOnTime) {
        if (!m_settings.schedule.contains(uuid)) {
            return false;
        }

        Schedule sched = m_settings.schedule[uuid];

        if (!sched.bOnTime) return false;

        if (sched.startTime <= QDateTime::currentDateTime()) {
            bool bDoBackup = true;

            if (sched.bRetry) {
                QDateTime retry = sched.lastTry;
                if (sched.retryType == "minute") {
                    retry = retry.addSecs(sched.nRetryNumber * 60);
                } else if (sched.retryType == "hour") {
                    retry = retry.addSecs(sched.nRetryNumber * 3600);
                } else if (sched.retryType == "day") {
                    retry = retry.addDays(sched.nRetryNumber);
                } else if (sched.retryType == "week") {
                    retry = retry.addDays(sched.nRetryNumber*7);
                } else if (sched.retryType == "month") {
                    retry = retry.addMonths(sched.nRetryNumber);
                } else if (sched.retryType == "year") {
                    retry = retry.addYears(sched.nRetryNumber);
                }
                bDoBackup &= (retry <= QDateTime::currentDateTime());
                if (!bDoBackup) return false;
            }
            bDoBackup = bDoBackup && (sched.bSilent ? true : askForBackup(fileName, type));

            QDateTime condDate = sched.startTime;
            while (condDate < QDateTime::currentDateTime()) {
                if (sched.everyType == "minute") {
                    condDate = condDate.addSecs(sched.nEveryCount * 60);
                } else if (sched.everyType == "hour") {
                    condDate = condDate.addSecs(sched.nEveryCount * 3600);
                } else if (sched.everyType == "day") {
                    condDate = condDate.addDays(sched.nEveryCount);
                } else if (sched.everyType == "week") {
                    condDate = condDate.addDays(sched.nEveryCount*7);
                } else if (sched.everyType == "month") {
                    condDate = condDate.addMonths(sched.nEveryCount);
                } else if (sched.everyType == "year") {
                    condDate = condDate.addYears(sched.nEveryCount);
                }
            }

            if (bDoBackup && uuid == getQtdUuid(fileName)) {
                bool bServerIsRunning = isServerRunning();
                QString toolTip = "QtdSync Monitor: " + fileName;
#ifdef WIN32
                toolTip += "\nQtdSync Server: " + (bServerIsRunning ? tr("Running.") : tr("Stopped!"));
#endif
                m_pTrayIcon->setToolTip(toolTip);
                m_pTrayIcon->setIcon(QIcon(bServerIsRunning ? ":/images/schedule_on_server.png" : ":/images/schedule_on.png"));
                QMap<QString, QString>config;
                config["silent"] = sched.bSilent ? "true":"false";
                config["markFailedAsValid"] = sched.bMarkFailedAsValid ? "true" : "false";
                config["scheduled"] = "true";

                bFailed = !(bBackupDone = doBackup(QStringList(fileName), config));
                toolTip = "QtdSync Monitor: " + (m_settings.monitor.bActive ? tr("Active"):tr("Not Active"));
#ifdef WIN32
                toolTip += "\nQtdSync Server: " + (bServerIsRunning ? tr("Running.") : tr("Stopped!"));
#endif
                m_pTrayIcon->setToolTip(toolTip);
                QString iconFile = m_settings.monitor.bActive ? ":/images/schedule" : ":/images/schedule_off";
                if (bServerIsRunning) {
                    iconFile += "_server";
                }
                m_pTrayIcon->setIcon(QIcon(iconFile + ".png"));

            }

            if (openQtd(fileName)) {
                m_config.schedule.startTime = (!bFailed || !sched.bRetry) ? condDate : sched.startTime;
                m_config.schedule.lastTry   = (!bFailed || !sched.bRetry) ? condDate : QDateTime::currentDateTime();
                m_config.unSaved = true;
                if (saveQtd()) {
                    bBackupDone = true;
                }
            }
        }
    } else {
        if (!openQtd(fileName)) {
            return false;
        }

        if (m_config.uuid != uuid) {
            return false;
        }

        if (!m_config.schedule.bOnPlugin) {
            return false;
        }

        QDateTime condDate;
        bool bLastFailed = m_config.lastBackupTry != m_config.lastBackup;
        if (m_config.lastBackupTry.isValid()) {
            condDate = m_config.lastBackupTry;
        } else {
            condDate = QDateTime();
        }
        QDateTime retry = condDate;

        if (m_config.schedule.conditionType == "minute") {
            condDate = condDate.addSecs(m_config.schedule.nConditionCount * 60);
        } else if (m_config.schedule.conditionType == "hour") {
            condDate = condDate.addSecs(m_config.schedule.nConditionCount * 3600);
        } else if (m_config.schedule.conditionType == "day") {
            condDate = condDate.addDays(m_config.schedule.nConditionCount);
        } else if (m_config.schedule.conditionType == "week") {
            condDate = condDate.addDays(m_config.schedule.nConditionCount*7);
        } else if (m_config.schedule.conditionType == "month") {
            condDate = condDate.addMonths(m_config.schedule.nConditionCount);
        } else if (m_config.schedule.conditionType == "year") {
            condDate = condDate.addYears(m_config.schedule.nConditionCount);
        }

        if (m_config.schedule.bRetry) {
            if (bLastFailed) {
                if (m_config.schedule.retryType == "minute") {
                    retry = retry.addSecs(m_config.schedule.nRetryNumber * 60);
                } else if (m_config.schedule.retryType == "hour") {
                    retry = retry.addSecs(m_config.schedule.nRetryNumber * 3600);
                } else if (m_config.schedule.retryType == "day") {
                    retry = retry.addDays(m_config.schedule.nRetryNumber);
                } else if (m_config.schedule.retryType == "week") {
                    retry = retry.addDays(m_config.schedule.nRetryNumber*7);
                } else if (m_config.schedule.retryType == "month") {
                    retry = retry.addMonths(m_config.schedule.nRetryNumber);
                } else if (m_config.schedule.retryType == "year") {
                    retry = retry.addYears(m_config.schedule.nRetryNumber);
                }
            }
        }


        bool bTimerCondition = false;
        if (bLastFailed && m_config.schedule.bRetry) {
            bTimerCondition = condDate < QDateTime::currentDateTime() || retry < QDateTime::currentDateTime();
        } else {
            bTimerCondition = condDate < QDateTime::currentDateTime();
        }

        if (m_config.schedule.bOnPlugin && (type == eOnPlugin || type == eOnStartup || type == eOnShutdown)) {
            if (bTimerCondition && (m_config.schedule.bSilent || !(bBackupDone = !askForBackup(fileName, type)))) {
                bool bServerIsRunning = isServerRunning();
                m_pTrayIcon->setIcon(QIcon(bServerIsRunning ? ":/images/schedule_on_server.png" : ":/images/schedule_on.png"));
                QString toolTip = "QtdSync Monitor: " + fileName;
#ifdef WIN32
                toolTip += "\nQtdSync Server: " + (bServerIsRunning ? tr("Running.") : tr("Stopped!"));
#endif
                m_pTrayIcon->setToolTip(toolTip);

                QMap<QString, QString>config;
                config["silent"] = m_config.schedule.bSilent ? "true":"false";
                config["scheduled"] = "true";
                bFailed = !(bBackupDone = doBackup(QStringList(fileName), config));
                bBackupDone |= !m_config.schedule.bRetry;

                QString iconFile = m_settings.monitor.bActive ? ":/images/schedule" : ":/images/schedule_off";
                if (bServerIsRunning) {
                    iconFile += "_server";
                }
                m_pTrayIcon->setIcon(QIcon(iconFile + ".png"));
                toolTip = "QtdSync Monitor: " + (m_settings.monitor.bActive ? tr("Active"):tr("Not Active"));
#ifdef WIN32
                toolTip += "\nQtdSync Server: " + (bServerIsRunning ? tr("Running.") : tr("Stopped!"));
#endif
                m_pTrayIcon->setToolTip(toolTip);
            }
        }
    }

    return bBackupDone;
}

//----------------------------------------------------------------------------
void QtdSync::slot_monitor()
{
    qApp->processEvents();
    slot_checkForUpdate();

    if (QtdTools::getAllProcesses(EXEC("rsync")).count() > 0 && m_settings.monitor.nServerProcessId == 0) {
        getCurrentServerPid();
    }
    updateServerStatus();


    if (QtdTools::getAllProcesses(QFileInfo(qApp->applicationFilePath()).fileName()).count() > 1) {
        // postpone backups until all other clients are closed
        return;
    }

    m_timer->stop();

    // check for file modifications
    foreach (QString file, m_settings.knownQtd.keys()) {
        if (m_settings.lastModified[m_settings.knownQtd[file]] < QFileInfo(file).lastModified()) {
            openQtd(file);
        }
    }

    // check plugin
    static int count = 0;
    QStringList failedBackups;

    QPair<QStringList, QStringList> remAdd = validateKnownQtd();
    QMap<QString, bool> done;
    bool bFailed = false;
    foreach(QString qtd, remAdd.second) {
        bFailed = false;
        done[m_settings.knownQtd[qtd]] = doSchedulePlugin(qtd, m_settings.knownQtd[qtd], eOnPlugin, bFailed);
        if (bFailed && !failedBackups.contains(qtd)) {
            failedBackups << qtd;
        }
    }

    foreach(QString rem, m_currentRemoved) {
        bFailed = false;
        if (!remAdd.first.contains(rem) && !done.contains(m_settings.knownQtd[rem])) {
            if (!(done[m_settings.knownQtd[rem]] = doSchedulePlugin(rem, m_settings.knownQtd[rem], eOnPlugin, bFailed))) {
                if (bFailed && !failedBackups.contains(rem)) {
                    failedBackups << rem;
                }
                remAdd.first.append(rem);
            }
        }
    }
    m_currentRemoved = remAdd.first;
    count++;

    // check for scheduled
    QMap<QString, Schedule>::iterator it = m_settings.schedule.begin();
    QMap<QString, Schedule> newScheds;
    for (it; it != m_settings.schedule.end(); it++) {
        bFailed = false;
        QString uuid = it.key();
        QString fileName = m_settings.knownQtd.key(uuid);
        if (m_settings.schedule[uuid].bOnTime && (done[uuid] = doSchedulePlugin(fileName, uuid, eOnTime, bFailed))) {
            newScheds.insert(uuid, m_config.schedule);
        }
        if (bFailed && !failedBackups.contains(fileName)) {
            failedBackups << fileName;
        }

    }

    if (m_pTrayIcon && failedBackups.count() > 0) {
        m_pTrayIcon->showMessage(tr("Backups failed"), tr("The backups\n\n- %1\n\nfailed!").arg(failedBackups.join("\n- ")), QSystemTrayIcon::Warning);
    }

    foreach (QString key, newScheds.keys()) {
        m_settings.schedule[key] = newScheds[key];
    }

    m_timer->start(5000);
}

//----------------------------------------------------------------------------
void QtdSync::slot_monitorServer()
{
    m_timer->stop();

    static int count = 0;
    count++;

    if (count > 10) {
        unsigned long pid = m_settings.monitor.nServerProcessId;
        getCurrentServerPid();
        if (m_settings.monitor.nServerProcessId == 0 && pid != 0) {
            loadConfig();
            updateServerUsers();
            updateServerDirectories();
        }
        count = 0;
    }

    updateServerStatus();

    m_timer->start(1000);
}


//----------------------------------------------------------------------------
void QtdSync::slot_monitorActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick) {
        slot_monitorOpenQtdSync();
    }
}

//----------------------------------------------------------------------------
void QtdSync::commitData(QSessionManager& manager)
{
#ifdef WIN32
    manager.release();
#endif
}

