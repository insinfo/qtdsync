#ifndef QTDAPPLICATION_H
#define QTDAPPLICATION_H

#include "QtdBase.h"

#ifdef QTD_HAVE_UPDATECLIENT
#include "QtdUpdateClient.h"
#else
# define QtdUpdateClient void
#endif

#ifndef QTD_UPDATE_LOCATION
# define QTD_UPDATE_LOCATION "http://www.doering-thomas.de/updates/%1.uif"
#endif

#include "QtdTools.h"

#ifdef WIN32
#include <windows.h>
#else
#include <sys/utsname.h>
#define OSVERSIONINFOA struct utsname
#endif

//----------------------------------------------------------------------------
class QtdApplication : public QApplication
{
    Q_OBJECT
public:
    typedef enum EnMessageType {
         eUnknown
        ,eInformation
        ,eWarning
        ,eError
    };

    typedef struct Message {
        QDateTime     time;
        QString       text;
        EnMessageType eType;
    };

    typedef QMap<QString, QString> QtdConfig;

    QtdApplication(int &argc, char** argv, QtdConfig config = QtdConfig());
    ~QtdApplication();

#ifdef WIN32
    virtual bool                winEventFilter(MSG* message, long* result);
#endif
    bool                        singleApplication(bool bTrigger = true);
    QString                     productTitle();
    QString                     versionId();

    Message                     peekNextMessage();
    Message                     takeNextMessage();
    QList<Message>              takeMessages(EnMessageType eType);

    static bool                 isElevated();
    static bool                 elevate();

signals:
    void                        messageQueued();

public slots:
    void                        queueMessage(int /* EnMessageType */ eType, QString message);

protected:
    int                         getSvnInfo(QString& repos, bool bShared = false);
    void                        info();
    void                        loadTranslations(QLocale::Language lang = (QLocale::Language)0);
    QString                     translationFile(QLocale::Language lang);
    QList<QLocale::Language>    availableTranslations();
    void                        showSvnInfo();
    void                        showUpdateLocation();
    void                        prepareTranslation();

    QString                     m_productName;
    QString                     m_productVersion;
    QString                     m_productCopyright;
    QString                     m_productUpdateVersion;
    QString                     m_productUpdateLocation;
    QtdTools::ProductInfo       m_productInfo;

    OSVERSIONINFOA              m_osVersionInfo;

    
    QtdUpdateClient*            m_updateClient;
    QMap<QString, QString>      m_applicationConfig;

    QTranslator                 m_translator;
    bool                        m_bDebug;
    bool                        m_bCustomBuild;

    QList<Message>              m_appMessages;

#ifdef WIN32
private:
    UINT                        m_nQtdBroadCastId;
#endif
};

#define qtdApp (qobject_cast<QtdApplication*>(qApp))

#endif // QTDAPPLICATION_H