#ifndef QTDMAIL_H
#define QTDMAIL_H

#include "QtdBase.h"

//----------------------------------------------------------------------------
class QtdMailPrivate : public QObject
{
    Q_OBJECT
public:
    QtdMailPrivate() : QObject() {}
    ~QtdMailPrivate() {}

protected slots:
    void slot_sslErrors(const QList<QSslError>&)
    {
        QSslSocket* pSocket = qobject_cast<QSslSocket*>(sender());
        if (pSocket) {
            pSocket->ignoreSslErrors();
        }
    }
};

//----------------------------------------------------------------------------
class QtdMail
{
public:
    //------------------------------------------------------------------------
    QtdMail(QString smtp = "");
    ~QtdMail();

    typedef enum {
         eMimePlain
        ,eMimeHTML
    } MimeType;

    typedef enum {
         eNoError
        ,eSendError_SmtpUndefined
        ,eSendError_ReceiverUndefined
        ,eSendError_SenderUndefined
        ,eSendError_WSAInitialization
        ,eSendError_SmtpNotFound
        ,eSendError_SocketInitialization
        ,eSendError_ServerConnection
        ,eSendError_Initial
        ,eSendError_Helo
        ,eSendError_MailFrom
        ,eSendError_MailTo
        ,eSendError_Data
        ,eSendError_Mail
        ,eSendError_Quit
        ,eSendError_UserName
        ,eSendError_Password
        ,eSendError_Unknown
    } SendError;

    //------------------------------------------------------------------------
    SendError sendMail();
    bool      isValid();
    //------------------------------------------------------------------------

    void    setSmtpServer(QString smtp, bool bUseEncryption = false);
    void    setSender(QString senderAddress, QString senderName = "");
    void    setReceiver(QString receiverAddress, QString receiverName = "");
    void    setMime(MimeType mime);
    void    setUserAuthenticationEnabled(bool bEnable, QString userName = "", QString password = "");

    void    setMail(QString body, QString subject = "");
    void    setMailSubject(QString subject);
    void    setMailBody(QString body);

    QString smtp();
    QString receiverAddress();
    QString receiverName();
    QString senderAddress();
    QString senderName();
    MimeType mime();
    QString subject();
    QString body();
    bool    authenticationEnabled();
    bool    encryptionEnabled();
    QString user();
    QString password();

    bool        toXml(QDomElement& elm);
    void        fromXml(QDomElement elm);

protected:
    QString     m_smtp;
    unsigned int m_nTimeout;
    QString     m_senderAddr;
    QString     m_senderName;
    QString     m_receiverAddr;
    QString     m_receiverName;
    MimeType    m_mime;
    QString     m_mailSubject;
    QString     m_mailBody;
    bool        m_bUserAuth;
    QString     m_userName;
    QString     m_password;
    bool        m_bUseEncryption;
};

#endif // QTDMAIL_H
