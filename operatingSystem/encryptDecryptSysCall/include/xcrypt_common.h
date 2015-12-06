#ifndef _XCRYPT_COMMON_H
#define _XCRYPT_COMMON_H

const int ENCRYPT=1;
const int DECRYPT=2;

typedef struct
{
    int op;
    const char *passphrase,*cipheralgo,*input,*output;
}xcrypt_arg_t;

#endif
