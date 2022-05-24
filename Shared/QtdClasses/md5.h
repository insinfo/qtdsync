#ifndef _MD5_H
#define _MD5_H

#ifndef uint8
#define uint8  unsigned char
#endif

#ifndef uint32
#define uint32 unsigned int
#endif

typedef struct
{
    uint32 total[2];
    uint32 state[4];
    uint8 buffer[64];
}
md5_context;

void calcMd5(char *data, unsigned int nDataLen, unsigned char *hash);

void md5_starts( md5_context *ctx );
void md5_update( md5_context *ctx, uint8 *input, uint32 length );
void md5_finish( md5_context *ctx, uint8 digest[16] );

static char *msg[] = 
{
    "",
    "a",
    "abc",
    "message digest",
    "abcdefghijklmnopqrstuvwxyz",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
    "12345678901234567890123456789012345678901234567890123456789012" \
        "345678901234567890"
};

static char *val[] =
{
    "d41d8cd98f00b204e9800998ecf8427e",
    "0cc175b9c0f1b6a831c399e269772661",
    "900150983cd24fb0d6963f7d28e17f72",
    "f96b697d7cb7938d525a2f31aaf161d0",
    "c3fcd3d76192e4007dfb496cca67e13b",
    "d174ab98d277d9f5a5611c2c9f419d9f",
    "57edf4a22be3c955ac49da2e2107b67a"
};

#endif /* md5.h */