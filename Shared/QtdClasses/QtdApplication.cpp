#include "QtdApplication.h"
#include "QtdCrypt.h"
#include "QtdBase.h"

#ifdef HAVE_SVN_REVISION
#include "svnrevision.h"
#include "svnrevision_shared.h"
#endif

#ifdef WIN32
#include "windows.h"
#pragma comment(lib, "version.lib")
#endif


#define QT_LINGUIST "http://tdoering.kilu.de/linguist.exe"
//----------------------------------------------------------------------------
// init functions
//----------------------------------------------------------------------------
QtdApplication::QtdApplication(int& argc, char** argv, QMap<QString, QString> config)
: QApplication(argc, argv)
, m_applicationConfig(config)
, m_updateClient(0L)
, m_bDebug(false)
, m_bCustomBuild(true)
#ifdef WIN32
, m_nQtdBroadCastId(0)
#endif
{
#ifdef WIN32
    QString msgId = applicationFilePath() + "_qtd";
    if (m_applicationConfig.contains("--useSingleApplicationIdentifier")) {
        msgId = m_applicationConfig["--useSingleApplicationIdentifier"];
    }
    m_nQtdBroadCastId = RegisterWindowMessageA(msgId.toLatin1().data());
#endif

    info();

#ifdef WIN32
    ZeroMemory(&m_osVersionInfo, sizeof(OSVERSIONINFOA));
    m_osVersionInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
    GetVersionExA(&m_osVersionInfo);
#else
    uname(&m_osVersionInfo);
#endif


    if (m_applicationConfig.contains("--language")) {
        QLocale lang(m_applicationConfig["--language"]);
        loadTranslations(lang.language());
    } else {
        loadTranslations();
    }

#ifdef QTD_HAVE_UPDATECLIENT
    QString updatedFrom("");
    if (m_applicationConfig.contains("--justUpdatedFrom")) {
        updatedFrom = m_applicationConfig["--justUpdatedFrom"];
        m_applicationConfig.remove("--justUpdatedFrom");
    }

    if (m_applicationConfig.contains("--useUpdateVersion")) {
        m_productUpdateVersion = m_applicationConfig["--useUpdateVersion"];
    }

    if (m_applicationConfig.contains("--useUpdateLocation")) {
        m_productUpdateLocation =  m_applicationConfig["--useUpdateLocation"];
    }

    m_updateClient = new QtdUpdateClient(m_productUpdateLocation, m_productUpdateVersion, updatedFrom);
#endif

    if (m_applicationConfig.contains("--showSecretSvnInfo")) {
        m_applicationConfig.remove("--showSecretSvnInfo");
        showSvnInfo();
    }

    if (m_applicationConfig.contains("--showSecretUpdateLocation")) {
        m_applicationConfig.remove("--showSecretUpdateLocation");
        showUpdateLocation();
    }

    QStringList infoMessages;
    foreach (QString key, m_applicationConfig.keys()) {
        if (key.startsWith("--showInfoMessage")) {
            if (key.startsWith("--showInfoMessageSpecial")) {
                infoMessages << translate("QtdSpecialMessages", m_applicationConfig[key].toLatin1().data());
            } else {
                infoMessages << m_applicationConfig[key];
            }
        }
    }

    if (infoMessages.count() > 0) {
        QMessageBox::information(0L, m_productName + " v" + m_productVersion, infoMessages.join("<br><br>"));
        if (m_applicationConfig.contains("--quitAfterMessages")) {
            ExitProcess(0);
        }
    }

    if (m_applicationConfig.contains("--useDebuggingFeatures")) {
        m_applicationConfig.remove("--useDebuggingFeatures");
        m_bDebug = true;
    }

    if (m_applicationConfig.contains("--prepareTranslation")) {
        prepareTranslation();
    }

    // check for custom build
    QByteArray ba = QByteArray::fromBase64("D2vad8/YCLKL2wQPy0Nrxnn9mEY8xZTFVAPbP1HKMbBrlE/YfgGYjW+XKXS95Ncb");
    QByteArray out;
    if (QtdCrypt::decrypt(ba, QTD_PASS_HASH)) {
        m_bCustomBuild = (QTD_PASS_HASH != ba);
    }

    if (m_bCustomBuild) {
        m_productVersion += " (Custom Build)";
    }


}

//----------------------------------------------------------------------------
QtdApplication::~QtdApplication()
{
#ifdef QTD_HAVE_UPDATECLIENT
    if (m_updateClient) {
        delete m_updateClient;
    }
#endif
}

//----------------------------------------------------------------------------
bool QtdApplication::elevate()
{
#ifdef WIN32
    if (isElevated()) {
        return true;
    }

    QString args("");
    if (arguments().count() > 1) {
        QStringList agrList = arguments().mid(1);
        args = "\"" + agrList.join("\" \"") + "\"";
    }
    ShellExecuteA(NULL, "runas", applicationFilePath().toLatin1().data(), args.toLatin1().data(), NULL, SW_SHOW);

    return false;
#else
    return true;
#endif
}

//----------------------------------------------------------------------------
bool QtdApplication::isElevated()
{
#ifdef WIN32
    DWORD dwSize = 0;
    HANDLE hToken = NULL;
    BOOL bReturn = FALSE;
 
    TOKEN_ELEVATION tokenInformation;
     
    if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return FALSE;
 
    if(GetTokenInformation(hToken, TokenElevation, &tokenInformation, sizeof(TOKEN_ELEVATION), &dwSize))
    {
        bReturn = (BOOL)tokenInformation.TokenIsElevated;
    }
 
    CloseHandle(hToken);
    return bReturn;
#else
    return true;
#endif
}

//----------------------------------------------------------------------------
QList<QtdApplication::Message> QtdApplication::takeMessages(EnMessageType eType)
{
    QList<Message> msgs;

    for (int i = 0; i < m_appMessages.count(); i++) {
        if (m_appMessages.at(i).eType == eType) {
            msgs << m_appMessages.takeAt(i);
            i--;
        }
    }

    return msgs;
}


//----------------------------------------------------------------------------
QtdApplication::Message QtdApplication::takeNextMessage()
{
    Message msg;
    msg.eType = eUnknown;
    msg.text = "";

    if (m_appMessages.count() > 0) {
        return m_appMessages.takeFirst();
    } else {
        return msg;
    }
}

//----------------------------------------------------------------------------
QtdApplication::Message QtdApplication::peekNextMessage()
{
    Message msg;
    msg.eType = eUnknown;
    msg.text = "";

    if (m_appMessages.count() > 0) {
        return m_appMessages.first();
    } else {
        return msg;
    }
}

//----------------------------------------------------------------------------
void QtdApplication::queueMessage(int /* EnMessageType */ eType, QString message)
{
    Message msg;
    msg.text = message;
    msg.time = QDateTime::currentDateTime();
    msg.eType = (EnMessageType)eType;

    m_appMessages.append(msg);

    emit messageQueued();
}


#ifdef WIN32
//----------------------------------------------------------------------------
bool QtdApplication::winEventFilter(MSG* message, long* result)
{
    bool bRet = false;
    if (message->message == m_nQtdBroadCastId) {
        if (message->wParam == 0 && message->lParam == 0) {
            // triggerSingleApplication
            QWidget* pWidget = QWidget::find(message->hwnd);

            if (pWidget && !qobject_cast<QMainWindow*>(pWidget) && !qobject_cast<QDialog*>(pWidget)) {
                pWidget = NULL;
            }

            if (pWidget) {
                pWidget->show();
                pWidget->raise();
                pWidget->activateWindow();
            }
            *result = 1;
            bRet = true;
        }
    }
    if (!bRet) {
        return QApplication::winEventFilter(message, result);
    }

    return bRet;
}
#endif

//----------------------------------------------------------------------------
bool QtdApplication::singleApplication(bool bTrigger)
{
    QString appPath = applicationFilePath();
#ifdef WIN32
    appPath.replace("/", "\\");
#endif
    QMap<unsigned long, QString> proc = QtdTools::getAllProcesses(appPath);
    if (proc.count() > 1) {
#ifdef WIN32
        if (bTrigger) {
            SendMessage(HWND_BROADCAST, m_nQtdBroadCastId, 0, 0);
        }
#endif
        return false;
    } else {
        return true;
    }
}

//----------------------------------------------------------------------------
QString QtdApplication::productTitle()
{
    return m_productName + " v" + m_productVersion;
}

//----------------------------------------------------------------------------
QString QtdApplication::versionId()
{
    return m_productUpdateVersion;
}

//----------------------------------------------------------------------------
void QtdApplication::prepareTranslation()
{
#ifdef WIN32
    QDir::setCurrent(qApp->applicationDirPath());

    QMap<QString, QLocale::Language> languages;
    for (int i = 2; i <= QLocale::LastLanguage; i++) {
        QLocale loc((QLocale::Language)i);
        QString langName = loc.name();

        if ((QLocale::Language)i != QLocale::system().language() && langName == QLocale::system().name()) {
            continue;
        }
        languages[QLocale::languageToString((QLocale::Language)i)] = (QLocale::Language)i;
    }

    QGridLayout *gridLayout;
    QLabel *label;
    QComboBox *m_pChB_Languages;
    QFrame *line;
    QSpacerItem *horizontalSpacer;
    QPushButton *m_pPB_Ok;
    QPushButton *m_pPB_Cancel;

    QDialog* Dialog = new QDialog(0L, Qt::Tool);
    Dialog->resize(251, 78);
    gridLayout = new QGridLayout(Dialog);
    gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
    label = new QLabel(Dialog);
    label->setObjectName(QString::fromUtf8("label"));

    gridLayout->addWidget(label, 0, 0, 1, 1);

    m_pChB_Languages = new QComboBox(Dialog);
    m_pChB_Languages->setObjectName(QString::fromUtf8("m_pChB_Languages"));
    m_pChB_Languages->addItems(languages.keys());

    gridLayout->addWidget(m_pChB_Languages, 0, 1, 1, 3);

    line = new QFrame(Dialog);
    line->setObjectName(QString::fromUtf8("line"));
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);

    gridLayout->addWidget(line, 1, 0, 1, 4);

    horizontalSpacer = new QSpacerItem(64, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

    gridLayout->addItem(horizontalSpacer, 2, 0, 1, 2);

    m_pPB_Ok = new QPushButton(Dialog);
    m_pPB_Ok->setObjectName(QString::fromUtf8("m_pPB_Ok"));

    gridLayout->addWidget(m_pPB_Ok, 2, 2, 1, 1);

    m_pPB_Cancel = new QPushButton(Dialog);
    m_pPB_Cancel->setObjectName(QString::fromUtf8("m_pPB_Cancel"));

    gridLayout->addWidget(m_pPB_Cancel, 2, 3, 1, 1);
    
    Dialog->setWindowTitle(m_productName + " Translation");
    label->setText("Language");
    m_pPB_Ok->setText("Ok");
    m_pPB_Cancel->setText("Cancel");

    QObject::connect(m_pPB_Ok, SIGNAL(clicked()), Dialog, SLOT(accept()));
    QObject::connect(m_pPB_Cancel, SIGNAL(clicked()), Dialog, SLOT(reject()));

    if (Dialog->exec() == QDialog::Accepted) {
        QLocale loc(languages[m_pChB_Languages->currentText()]);

        QString inFileName(":/translations/" + m_productName.toLower() + "_" + loc.name() + ".ts");
        QString outFileName("translations/" + m_productName.toLower() + "_" + loc.name() + ".ts");
        QString content = QtdTools::readFile(inFileName);
        if (content == "") {
            inFileName = ":/translations/" + m_productName.toLower() + "_en_US.ts";
            content = QtdTools::readFile(inFileName);
            content.replace("language=\"en\"", QString("language=\"%1\"").arg(loc.name()));
        }
        if (content != "") {
            if (!QDir("translations").exists()) {
                QDir::current().mkdir("translations");
            }

            QtdTools::writeFile("translations/QtdSyncTranslationHelp.txt", QtdTools::readFile(":/translations/QtdSyncTranslationHelp.txt"));

            bool bWrite = true;
            if (QFile(outFileName).exists()) {
                bWrite = QMessageBox::question(0L, "File exists", QString("The translation file <b>%1</b> already exists! Overwrite?").arg(outFileName), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
            }
            if (bWrite) {
                QtdTools::writeFile(outFileName, content);

                if (!QDir("forms").exists()) {
                    QDir::current().mkdir("forms");
                }
                QStringList forms = QDir(":/forms").entryList();
                foreach (QString form, forms) {
                    QString outFormFileName = "forms/" + form;
                    if (!QFile(outFormFileName).exists()) {
                        QtdTools::writeFile(outFormFileName, QtdTools::readFile(":/forms/" + form));
                    }
                }

                if (!QFile("linguist.exe").exists()) {
                    QMessageBox::question(0L, "Qt Linguist", QString("You'll need <b>Qt Linguist</b> to translate <b>%1</b>.<br><br>Please download it into you application dir<br><br><b>%2</b>!").arg(m_productName).arg(qApp->applicationDirPath()));
                    QUrl url(QT_LINGUIST);
                    QDesktopServices::openUrl(url);
                } else {
                    ShellExecuteA(0, "open", "linguist.exe", outFileName.toLatin1().data(), "", SW_SHOW);
                }
                ShellExecuteA(0, "open", "translations/QtdSyncTranslationHelp.txt", "", "", SW_SHOW);
            }
        }
    }

    delete Dialog;

    ExitProcess(0);
#endif

}

//----------------------------------------------------------------------------
void QtdApplication::loadTranslations(QLocale::Language lang)
{
    QString filename;
    if (lang > 0) {
        filename = translationFile(lang);
    } else {
        filename = translationFile(QLocale::system().language());
    }
    
    //if translation file doesn't exists, try to get it from qt file system
    if(QFile::exists(filename)) {
        // load appropriate language file
        m_translator.load(filename);
        qApp->removeTranslator(&m_translator);
        qApp->installTranslator(&m_translator);
    }
}

//----------------------------------------------------------------------------
QList<QLocale::Language> QtdApplication::availableTranslations()
{
    int i;
    QList<QLocale::Language> lst;

    for (i = 1; i <= QLocale::LastLanguage; i++) {
        if (translationFile((QLocale::Language)i) != "") {
            lst << (QLocale::Language)i;
        }
    }

    return lst;
}

//----------------------------------------------------------------------------
QString QtdApplication::translationFile(QLocale::Language lang)
{
    QString internalFile = ":/translations/" + m_productName.toLower() + "_";
    QString externalFile = qApp->applicationDirPath() + "/translations/" + m_productName.toLower() + "_";
    QString langName = QLocale(lang).name();

    if (lang != QLocale::system().language() && langName == QLocale::system().name()) {
        return "";
    }

    if (QFile::exists(externalFile + langName+".qm")) {
        return externalFile + langName+".qm";
    } else if (QFile::exists(internalFile + langName + ".qm")) {
        return internalFile + langName+".qm";
    } else
        return "";
}


//----------------------------------------------------------------------------
void QtdApplication::info()
{
    QString name = qApp->applicationFilePath();

    QtdTools::productInfo(qApp->applicationFilePath(), m_productInfo);
    m_productName = m_productInfo.name;

    qApp->setApplicationName(m_productName);
    qApp->setApplicationVersion(m_productVersion);

    m_productVersion = m_productInfo.versionName;
    m_productUpdateVersion = QString("%1.%2.%3")
        .arg(m_productInfo.version[0], 3, 10, QChar('0'))
        .arg(m_productInfo.version[1], 3, 10, QChar('0'))
        .arg(m_productInfo.version[2], 3, 10, QChar('0'));

    m_productCopyright = m_productInfo.copyright;
    m_productUpdateLocation = QString(QTD_UPDATE_LOCATION).arg(m_productName);
}

//----------------------------------------------------------------------------
int QtdApplication::getSvnInfo(QString& repository, bool bShared)
{
    if (!bShared) {
#if defined(SVNREVISION) && defined(SVNREPOSITORY)
        repository = SVNREPOSITORY; 
        return SVNREVISION; 
#endif
    } else {
#if defined(QTDSHARED_SVNREPOSITORY) && defined(QTDSHARED_SVNREVISION)
        repository = QTDSHARED_SVNREPOSITORY; 
        return QTDSHARED_SVNREVISION; 
#endif
    }
    return -1;
}

//----------------------------------------------------------------------------
void QtdApplication::showSvnInfo()
{
    QString repository;
    int nRevision = getSvnInfo(repository);

    QString repositoryShared;
    int nRevisionShared = getSvnInfo(repositoryShared, true);

    if (nRevision != -1) {
        QMessageBox::information(0L, "SVN Info", QString("Application: %1 @ %2<br>Shared: %3 @ %4").arg(nRevision).arg(repository).arg(nRevisionShared).arg(repositoryShared));
    }
}

//----------------------------------------------------------------------------
void QtdApplication::showUpdateLocation()
{
#ifdef QTD_HAVE_UPDATECLIENT
    if (m_updateClient) {
        QString uil = m_updateClient->updateLocation();

        QMessageBox::information(0L, "UpdateLocation", QString("%1").arg(uil));
    }
#endif
}
