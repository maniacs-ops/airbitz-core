/**
 * @file
 * Password-based login logic.
 */

#ifndef ABC_LoginPassword_h
#define ABC_LoginPassword_h

#include "ABC.h"
#include "ABC_Login.h"

namespace abcd {

    tABC_CC ABC_LoginPassword(tABC_Login **ppSelf,
                              const char *szUserName,
                              const char *szPassword,
                              tABC_Error *pError);

    tABC_CC ABC_LoginPasswordSet(tABC_Login *pSelf,
                                 const char *szPassword,
                                 tABC_Error *pError);

    tABC_CC ABC_LoginPasswordOk(tABC_Login *pSelf,
                                const char *szPassword,
                                bool *pOk,
                                tABC_Error *pError);

} // namespace abcd

#endif
