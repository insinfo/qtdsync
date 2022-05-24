#include "QtdCrypt.h"

#include "AES.cpp"
#include "md5.cpp"

/* Note: This is just a wrapper for
            - aes encryption
            - md5
*/

//------------------------------------------------------------------------------
QString QtdCrypt::md5(QByteArray& data)
{
    unsigned char hash[16] = {0};
    
    // calc md5
    calcMd5(data.data(), data.count(), hash);

    // create return string
    QString hashStr;
    for (int i = 0; i < 16; i++) {
        hashStr += QString("%1").arg((int)hash[i], 2, 16, QChar('0'));
    }

    return hashStr;
}

//------------------------------------------------------------------------------
bool QtdCrypt::encrypt(QByteArray& inData, QString passHash, QByteArray* pOut)
{
    AES aes;
    aes.SetParameters(128);
    aes.StartEncryption((const unsigned char*)passHash.toLatin1().data());

    unsigned int nInDataLen = inData.count();
    unsigned int nBlocks    = (nInDataLen + sizeof(unsigned int) /* nLength */ + 15) / 16;

    unsigned char* pOutData = new unsigned char[nBlocks * 16];
    unsigned char* pInData  = new unsigned char[nBlocks * 16];

    memcpy(pInData,                        &nInDataLen,     sizeof(unsigned int));
    memcpy(pInData + sizeof(unsigned int), inData.data(),   nInDataLen);

    // do the encryption
    aes.Encrypt(pInData, pOutData, nBlocks);

    if (pOut) {
        *pOut  = QByteArray((const char*)pOutData, nBlocks * 16);
    } else {
        inData = QByteArray((const char*)pOutData, nBlocks * 16);
    }

    delete [] pOutData;
    delete [] pInData;

    return true;
}

//------------------------------------------------------------------------------
bool QtdCrypt::decrypt(QByteArray& inData, QString passHash, QByteArray* pOut)
{
    AES aes;
    aes.SetParameters(128);
    aes.StartDecryption((const unsigned char*)passHash.toLatin1().data());

    unsigned int nInDataLen = inData.count();
    unsigned int nBlocks    = (nInDataLen + 15) / 16;

    unsigned char* pOutData = new unsigned char[nBlocks * 16];
    unsigned char* pInData  = new unsigned char[nBlocks * 16];

    memcpy(pInData, inData.data(),   nInDataLen);

    // do the decryption
    aes.Decrypt(pInData, pOutData, nBlocks);

    // get real data len
    memcpy(&nInDataLen, pOutData, sizeof(unsigned int));

    // sanity check
    if (nInDataLen > (nBlocks * 16) - sizeof(unsigned int)) {
        // wrong hash provided?
        return false;
    }

    if (pOut) {
        *pOut  = QByteArray((const char*)(pOutData + sizeof(unsigned int)), nInDataLen);
    } else {
        inData = QByteArray((const char*)(pOutData + sizeof(unsigned int)), nInDataLen);
    }

    delete [] pOutData;
    delete [] pInData;

    return true;
}
