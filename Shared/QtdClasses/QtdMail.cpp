#include "QtdMail.h"
#include "QtdCrypt.h"
#include "QtdApplication.h"
#include "QtdBase.h"
#include "QtdPassHash.h"

//----------------------------------------------------------------------------
QtdMail::QtdMail(QString smtp)
: m_smtp(smtp)
, m_mime(eMimePlain)
, m_bUserAuth(false)
, m_bUseEncryption(false)
, m_nTimeout(5000)
{
}

//----------------------------------------------------------------------------
QtdMail::~QtdMail()
{
}

// Basic error checking for send() and recv() functions
#define Check(fnStatus, error) { int iStatus = fnStatus; if(!((iStatus != -1) && iStatus)) { p->deleteLater(); return error; } }
#ifndef NDEBUG
#define SendNReceive(msg, error) { \
    QString str = msg; \
    answer = ""; \
    qDebug() << QString("> ") + msg; \
    Check(smtpServer.write(str.toAscii(), str.length()), error); \
    while (smtpServer.waitForReadyRead(1000)) { \
        answer += smtpServer.readAll(); \
    } \
    qDebug() << QString("< ") + answer; \
    foreach (QString line, answer.split("\n", QString::SkipEmptyParts)) { \
        if (line.startsWith("5")) { \
            smtpServer.close(); \
            p->deleteLater(); \
            return error; \
        } \
    } \
}
#else
#define SendNReceive(msg, error) { \
    QString str = msg; \
    Check(smtpServer.write(str.toAscii(), str.length()), error); \
    while (smtpServer.waitForReadyRead(1000)) { \
        answer += smtpServer.readAll(); \
    } \
    foreach (QString line, answer.split("\n", QString::SkipEmptyParts)) { \
        if (line.startsWith("5")) { \
            smtpServer.close(); \
            p->deleteLater(); \
            return error; \
        } \
    } \
}
#endif

//----------------------------------------------------------------------------
bool QtdMail::isValid()
{
    return m_smtp != "" && m_receiverAddr != "" && m_senderAddr != "";
}

//----------------------------------------------------------------------------
QString priv_currentAddress()
{
    QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    foreach (QHostAddress add, addresses) {
        if (add.protocol() == QAbstractSocket::IPv4Protocol && add.toString() != "127.0.0.1") {
            return add.toString();
        }
    }

    return "127.0.0.1";
}

//----------------------------------------------------------------------------
QtdMail::SendError QtdMail::sendMail()
{
    if (m_smtp == "") {
        return eSendError_SmtpUndefined;
    }

    if (m_receiverAddr == "") {
        return eSendError_ReceiverUndefined;
    }

    if (m_senderAddr == "") {
        return eSendError_SenderUndefined;
    }

    QSslSocket smtpServer;
    QUrl       url("http://" + m_smtp);
    QByteArray data;
    QString    host = url.host();
    QString    answer;

    QtdMailPrivate* p = new QtdMailPrivate();

    p->connect(&smtpServer, SIGNAL(sslErrors(const QList<QSslError>&)), p, SLOT(slot_sslErrors(const QList<QSslError>&)));

    if (m_bUseEncryption) {
        smtpServer.connectToHostEncrypted(url.host(), url.port(465));
        if (!smtpServer.waitForEncrypted(m_nTimeout)) {
            smtpServer.close();
            return eSendError_ServerConnection;
        }
    } else {
        smtpServer.connectToHost(url.host(), url.port(25));
        if (!smtpServer.waitForConnected(m_nTimeout)) {
            smtpServer.close();
            return eSendError_ServerConnection;
        }
    }
    
    // Receive initial response from SMTP server
    bool bOk = false;
    QDateTime curDT = QDateTime::currentDateTime();
    while (smtpServer.waitForReadyRead()) {
        data = smtpServer.readLine();
        if (data.length() > 0) {
            if (data.startsWith("220 ")) {
                bOk = true;
            }
            break;
        }
    }

    if (!bOk) {
        return eSendError_ServerConnection;
    }

    SendNReceive(QString("EHLO %1\r\n").arg(priv_currentAddress()), eSendError_Helo);
    if (m_bUserAuth) {
        SendNReceive(QString("AUTH LOGIN\r\n"), eSendError_SmtpUndefined);
        SendNReceive(QString("%1\r\n").arg(QString(m_userName.toAscii().toBase64())), eSendError_UserName);
        SendNReceive(QString("%1\r\n").arg(QString(m_password.toAscii().toBase64())), eSendError_Password);
    }
    SendNReceive(QString("MAIL FROM:<%1>\r\n").arg(m_senderAddr), eSendError_MailFrom);
    SendNReceive(QString("RCPT TO:<%1>\r\n").arg(m_receiverAddr), eSendError_MailTo);

    SendNReceive("DATA\r\n", eSendError_Data);
    SendNReceive(QString("From: %1 <%2>\r\nTo: %3 <%4>\r\nSubject: %5\r\ncontent-type: text/%6; charset=us-ascii\r\n\r\n%7\r\n.\r\n")
        .arg(m_senderName == "" ? m_senderAddr : m_senderName)
        .arg(m_senderAddr)
        .arg(m_receiverName == "" ? m_receiverAddr : m_receiverName)
        .arg(m_receiverAddr)
        .arg(m_mailSubject)
        .arg(m_mime == eMimePlain ? "plain" : "html")
        .arg(m_mailBody), eSendError_Mail);

    SendNReceive("QUIT\r\n", eSendError_Quit);

    smtpServer.close();
    p->deleteLater();
    return eNoError;
}

//----------------------------------------------------------------------------
void QtdMail::setSmtpServer(QString smtp, bool bUseEncryption)
{
    m_smtp = smtp;
    m_bUseEncryption = bUseEncryption;
}


//----------------------------------------------------------------------------
void QtdMail::setSender(QString senderAddress, QString senderName)
{
    m_senderAddr = senderAddress;
    if (senderName != "") {
        m_senderName = senderName;
    }
}


//----------------------------------------------------------------------------
void QtdMail::setReceiver(QString receiverAddress, QString receiverName)
{
    m_receiverAddr = receiverAddress;
    if (receiverName != "") {
        m_receiverName = receiverName;
    }
}

//----------------------------------------------------------------------------
void QtdMail::setMime(MimeType mime)
{
    m_mime = mime;
}

//----------------------------------------------------------------------------
void QtdMail::setUserAuthenticationEnabled(bool bEnable, QString userName, QString password)
{
    m_bUserAuth = bEnable;
    m_userName = userName;
    m_password = password;
}

//----------------------------------------------------------------------------
void QtdMail::setMailSubject(QString subject)
{
    m_mailSubject = subject;
}

//----------------------------------------------------------------------------
void QtdMail::setMail(QString body, QString subject)
{
    setMailBody(body);
    setMailSubject(subject);
}

//----------------------------------------------------------------------------
void QtdMail::setMailBody(QString body)
{
    m_mailBody = body;
}

//----------------------------------------------------------------------------
QString QtdMail::smtp()
{
    return m_smtp;
}

//----------------------------------------------------------------------------
QString QtdMail::receiverAddress()
{
    return m_receiverAddr;
}

//----------------------------------------------------------------------------
QString QtdMail::receiverName()
{
    return m_receiverName;
}

//----------------------------------------------------------------------------
QString QtdMail::senderAddress()
{
    return m_senderAddr;
}

//----------------------------------------------------------------------------
QString QtdMail::senderName()
{
    return m_senderName;
}

//----------------------------------------------------------------------------
QtdMail::MimeType QtdMail::mime()
{
    return m_mime;
}

//----------------------------------------------------------------------------
QString QtdMail::subject()
{
    return m_mailSubject;
}

//----------------------------------------------------------------------------
QString QtdMail::body()
{
   return m_mailBody;
}

//----------------------------------------------------------------------------
QString QtdMail::user()
{
   return m_userName;
}

//----------------------------------------------------------------------------
QString QtdMail::password()
{
   return m_password;
}

//----------------------------------------------------------------------------
bool QtdMail::authenticationEnabled()
{
   return m_bUserAuth;
}

//----------------------------------------------------------------------------
bool QtdMail::encryptionEnabled()
{
    return m_bUseEncryption;
}

//----------------------------------------------------------------------------
bool QtdMail::toXml(QDomElement& node)
{
    if (!isValid()) {
        return false;
    }

    QDomDocument doc = node.ownerDocument();
    QDomElement condNode = doc.createElement("smtp");
    if (m_bUseEncryption) {
        condNode.setAttribute("encrypted", "true");
    }
    condNode.appendChild(doc.createTextNode(smtp()));
    node.appendChild(condNode);

    condNode = doc.createElement("address");
    condNode.appendChild(doc.createTextNode(senderAddress()));
    node.appendChild(condNode);

    if (authenticationEnabled()) {
        condNode = doc.createElement("authentication");
        QByteArray passStr = password().toAscii();
        QByteArray userStr = user().toAscii();

        QtdCrypt::encrypt(passStr, QTD_PASS_HASH);
        QtdCrypt::encrypt(userStr, QTD_PASS_HASH);

        condNode.setAttribute("password",   QString(passStr.toBase64()));
        condNode.setAttribute("user",       QString(userStr.toBase64()));

        node.appendChild(condNode);
    }

    return true;
}

//----------------------------------------------------------------------------
void QtdMail::fromXml(QDomElement nNode)
{
    if (!nNode.isNull()) {
        QDomElement cNode = nNode.firstChildElement("smtp");
        if (!cNode.isNull()) {
            m_bUseEncryption = cNode.attribute("encrypted", "false") == "true";
            setSmtpServer(cNode.text(), m_bUseEncryption);
        }
        cNode = nNode.firstChildElement("address");
        if (!cNode.isNull()) {
            setSender(cNode.text(), cNode.attribute("name", qtdApp->productTitle()));
            setReceiver(cNode.text());
        }
        cNode = nNode.firstChildElement("authentication");
        if (!cNode.isNull()) {
            QString user = cNode.attribute("user", "");
            QString pass = cNode.attribute("password", "");

            QByteArray passStr = QByteArray::fromBase64(pass.toAscii());
            QByteArray userStr = QByteArray::fromBase64(user.toAscii());

            QtdCrypt::decrypt(passStr, QTD_PASS_HASH);
            QtdCrypt::decrypt(userStr, QTD_PASS_HASH);

            setUserAuthenticationEnabled(true, QString(userStr), QString(passStr));
        } else {
            setUserAuthenticationEnabled(false);
        }
    }

}