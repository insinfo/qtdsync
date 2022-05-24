#ifndef QTDCRYPT_H
#define QTDCRYPT_H

#include "QtdBase.h"
/* Note: This is just a Qt wrapper for
            - aes encryption
            - md5
*/

// include a header file containing
// #define     QTD_PASS_HASH           "00000000000000000000000000000000"
// replacing the "000000..." with your own
// NOTE: any selfcompiled version will be incompatible with the official release

#include "QtdPassHash.h"

//----------------------------------------------------------------------------
class QtdCrypt
{
public:
    //------------------------------------------------------------------------
    static bool encrypt(QByteArray& inData, QString passHash, QByteArray* pOutData = 0L);
    static bool decrypt(QByteArray& inData, QString passHash, QByteArray* pOutData = 0L);
    //------------------------------------------------------------------------

    static QString     md5(QByteArray& data);
};

#endif // QTDCRYPT_H
