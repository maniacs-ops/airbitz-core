/**
 * @file
 * AirBitz Account functions.
 *
 * This file contains all of the functions associated with account creation,
 * viewing and modification.
 *
 * @author Adam Harris
 * @version 1.0
 */

#include <jansson.h>
#include "ABC_Login.h"
#include "ABC_LoginDir.h"
#include "ABC_LoginServer.h"
#include "ABC_Account.h"
#include "ABC_Util.h"
#include "ABC_FileIO.h"
#include "ABC_Crypto.h"
#include "ABC_Sync.h"
#include "ABC_Wallet.h"
#include "ABC_General.h"
#include "ABC_Mutex.h"

#define ACCOUNT_MK_LENGTH 32

#define ACCOUNT_CARE_PACKAGE_FILENAME           "CarePackage.json"
#define ACCOUNT_LOGIN_PACKAGE_FILENAME          "LoginPackage.json"

// CarePackage.json:
#define JSON_ACCT_ERQ_FIELD                     "ERQ"
#define JSON_ACCT_SNRP2_FIELD                   "SNRP2"
#define JSON_ACCT_SNRP3_FIELD                   "SNRP3"
#define JSON_ACCT_SNRP4_FIELD                   "SNRP4"

// LoginPackage.json:
#define JSON_ACCT_MK_FIELD                      "MK"
#define JSON_ACCT_SYNCKEY_FIELD                 "SyncKey"
#define JSON_ACCT_ELP2_FIELD                    "ELP2"
#define JSON_ACCT_ELRA3_FIELD                   "ELRA3"

// holds keys for a given account
typedef struct sAccountKeys
{
    int             accountNum; // this is the number in the account directory - Account_x
    char            *szUserName;
    char            *szPassword;
    char            *szRepoAcctKey;
    tABC_CryptoSNRP *pSNRP1;
    tABC_CryptoSNRP *pSNRP2;
    tABC_CryptoSNRP *pSNRP3;
    tABC_CryptoSNRP *pSNRP4;
    tABC_U08Buf     MK;
    tABC_U08Buf     L;
    tABC_U08Buf     L1;
    tABC_U08Buf     P;
    tABC_U08Buf     LP1;
    tABC_U08Buf     LRA;
    tABC_U08Buf     LRA1;
    tABC_U08Buf     L4;
    tABC_U08Buf     RQ;
    tABC_U08Buf     LP;
    tABC_U08Buf     LP2;
    tABC_U08Buf     LRA3;
} tAccountKeys;

typedef enum eABC_LoginKey
{
    ABC_LoginKey_L1,
    ABC_LoginKey_L4,
    ABC_LoginKey_LP1,
    ABC_LoginKey_LP2,
    ABC_LoginKey_MK,
    ABC_LoginKey_RepoAccountKey,
    ABC_LoginKey_RQ
} tABC_LoginKey;

// When a recovers password from on a new device, care package is stored either
// rather than creating an account directory
static char *gCarePackageCache = NULL;

// this holds all the of the currently cached account keys
static unsigned int gAccountKeysCacheCount = 0;
static tAccountKeys **gaAccountKeysCacheArray = NULL;


static tABC_CC ABC_LoginGetKey(const char *szUserName, const char *szPassword, tABC_LoginKey keyType, tABC_U08Buf *pKey, tABC_Error *pError);
static tABC_CC ABC_LoginFetch(const char *szUserName, const char *szPassword, tABC_Error *pError);
static tABC_CC ABC_LoginInitPackages(const char *szUserName, tABC_U08Buf L1, const char *szCarePackage, const char *szLoginPackage, char **szAccountDir, tABC_Error *pError);
static tABC_CC ABC_LoginRepoSetup(const tAccountKeys *pKeys, tABC_Error *pError);
static tABC_CC ABC_LoginFetchRecoveryQuestions(const char *szUserName, char **szRecoveryQuestions, tABC_Error *pError);
static tABC_CC ABC_LoginCreateCarePackageJSONString(const json_t *pJSON_ERQ, const json_t *pJSON_SNRP2, const json_t *pJSON_SNRP3, const json_t *pJSON_SNRP4, char **pszJSON, tABC_Error *pError);
static tABC_CC ABC_LoginGetCarePackageObjects(int AccountNum, const char *szCarePackage, json_t **ppJSON_ERQ, json_t **ppJSON_SNRP2, json_t **ppJSON_SNRP3, json_t **ppJSON_SNRP4, tABC_Error *pError);
static tABC_CC ABC_LoginCreateLoginPackageJSONString(const json_t *pJSON_MK, const json_t *pJSON_RepoAcctKey, const json_t *pJSON_ELP2, const json_t *pJSON_ELRA3, char **pszJSON, tABC_Error *pError);
static tABC_CC ABC_LoginUpdateLoginPackageJSONString(tAccountKeys *pKeys, tABC_U08Buf MK, const char *szRepoAcctKey, tABC_U08Buf LP2, tABC_U08Buf LRA3, char **szLoginPackage, tABC_Error *pError);
static tABC_CC ABC_LoginGetLoginPackageObjects(int AccountNum, const char *szLoginPackage, json_t **ppJSON_EMK, json_t **ppJSON_ESyncKey, json_t **ppJSON_ELP2, json_t **ppJSON_ELRA3, tABC_Error *pError);
static tABC_CC ABC_LoginCacheKeys(const char *szUserName, const char *szPassword, tAccountKeys **ppKeys, tABC_Error *pError);
static void    ABC_LoginFreeAccountKeys(tAccountKeys *pAccountKeys);
static tABC_CC ABC_LoginAddToKeyCache(tAccountKeys *pAccountKeys, tABC_Error *pError);
static tABC_CC ABC_LoginKeyFromCacheByName(const char *szUserName, tAccountKeys **ppAccountKeys, tABC_Error *pError);
static tABC_CC ABC_LoginUpdateLoginPackageFromServerBuf(int AccountNum, tABC_U08Buf L1, tABC_U08Buf LP1, tABC_Error *pError);
static tABC_CC ABC_LoginMutexLock(tABC_Error *pError);
static tABC_CC ABC_LoginMutexUnlock(tABC_Error *pError);

/**
 * Checks if the username and password are valid.
 *
 * If the login info is valid, the keys for this account
 * are also cached.
 * If the creditials are not valid, an error will be returned
 *
 * @param szUserName UserName for validation
 * @param szPassword Password for validation
 */
tABC_CC ABC_LoginCheckCredentials(const char *szUserName,
                                    const char *szPassword,
                                    tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(szPassword);

    // check that this is a valid user
    ABC_CHECK_RET(ABC_LoginDirExists(szUserName, pError));

    // cache up the keys
    ABC_CHECK_RET(ABC_LoginCacheKeys(szUserName, szPassword, NULL, pError));

exit:

    return cc;
}

/**
 * Signs into an account
 * This cache's the keys for an account
 */
tABC_CC ABC_LoginSignIn(const char *szUserName,
                        const char *szPassword,
                        tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    // Clear out any old data
    ABC_LoginClearKeyCache(NULL);

    // check that this is a valid user, ignore Error
    if (ABC_LoginDirExists(szUserName, NULL) != ABC_CC_Ok)
    {
        // Try the server
        ABC_CHECK_RET(ABC_LoginFetch(szUserName, szPassword, pError));
        ABC_CHECK_RET(ABC_LoginDirExists(szUserName, pError));
        ABC_CHECK_RET(ABC_LoginCheckCredentials(szUserName, szPassword, pError));
    }
    else
    {
        tAccountKeys *pKeys  = NULL;
        ABC_CHECK_RET(ABC_LoginCacheKeys(szUserName, NULL, &pKeys, pError));
        if (ABC_BUF_PTR(pKeys->P) == NULL)
        {
            ABC_BUF_DUP_PTR(pKeys->P, szPassword, strlen(szPassword));
        }
        if (ABC_BUF_PTR(pKeys->LP) == NULL)
        {
            ABC_BUF_DUP(pKeys->LP, pKeys->L);
            ABC_BUF_APPEND(pKeys->LP, pKeys->P);
        }
        if (ABC_BUF_PTR(pKeys->LP1) == NULL)
        {
            // LP1 = Scrypt(P, SNRP1)
            ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->LP, pKeys->pSNRP1, &(pKeys->LP1), pError));
        }

        tABC_CC cc_fetch = ABC_LoginUpdateLoginPackageFromServerBuf(pKeys->accountNum, pKeys->L1, pKeys->LP1, pError);
        // If it was not a bad password (possibly network issues),
        // we clear the local cache and try to log in locally
        if (cc_fetch == ABC_CC_BadPassword)
        {
            cc = cc_fetch;
            pError->code = cc_fetch;
            goto exit;
        }

        // check the credentials
        ABC_CHECK_RET(ABC_LoginCheckCredentials(szUserName, szPassword, pError));
    }

    // take this non-blocking opportunity to update the info from the server if needed
    ABC_CHECK_RET(ABC_GeneralUpdateInfo(pError));
exit:
    if (cc != ABC_CC_Ok)
    {
        ABC_LoginClearKeyCache(NULL);
    }

    return cc;
}

/**
 * Create and account
 */
tABC_CC ABC_LoginCreate(const char *szUserName,
                        const char *szPassword,
                        tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    tABC_GeneralInfo        *pGeneralInfo        = NULL;
    tABC_AccountSettings    *pSettings           = NULL;
    tAccountKeys            *pKeys               = NULL;
    tABC_SyncKeys           *pSyncKeys           = NULL;
    tABC_U08Buf             NULL_BUF             = ABC_BUF_NULL;
    json_t                  *pJSON_SNRP2         = NULL;
    json_t                  *pJSON_SNRP3         = NULL;
    json_t                  *pJSON_SNRP4         = NULL;
    json_t                  *pJSON_EMK           = NULL;
    json_t                  *pJSON_ESyncKey      = NULL;
    char                    *szCarePackage_JSON  = NULL;
    char                    *szLoginPackage_JSON = NULL;
    char                    *szJSON              = NULL;
    char                    *szERepoAcctKey      = NULL;
    char                    *szAccountDir        = NULL;
    char                    *szFilename          = NULL;

    int AccountNum = 0;
    tABC_U08Buf MK          = ABC_BUF_NULL;
    tABC_U08Buf RepoAcctKey = ABC_BUF_NULL;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));

    // check locally that the account name is available
    ABC_CHECK_RET(ABC_LoginDirGetNumber(szUserName, &AccountNum, pError));
    if (AccountNum >= 0)
    {
        ABC_RET_ERROR(ABC_CC_AccountAlreadyExists, "Account already exists");
    }

    // create an account keys struct
    ABC_ALLOC(pKeys, sizeof(tAccountKeys));
    ABC_STRDUP(pKeys->szUserName, szUserName);
    ABC_STRDUP(pKeys->szPassword, szPassword);

    // generate the SNRP's
    ABC_CHECK_RET(ABC_CryptoCreateSNRPForServer(&(pKeys->pSNRP1), pError));
    ABC_CHECK_RET(ABC_CryptoCreateSNRPForClient(&(pKeys->pSNRP2), pError));
    ABC_CHECK_RET(ABC_CryptoCreateSNRPForClient(&(pKeys->pSNRP3), pError));
    ABC_CHECK_RET(ABC_CryptoCreateSNRPForClient(&(pKeys->pSNRP4), pError));
    ABC_CHECK_RET(ABC_CryptoCreateJSONObjectSNRP(pKeys->pSNRP2, &pJSON_SNRP2, pError));
    ABC_CHECK_RET(ABC_CryptoCreateJSONObjectSNRP(pKeys->pSNRP3, &pJSON_SNRP3, pError));
    ABC_CHECK_RET(ABC_CryptoCreateJSONObjectSNRP(pKeys->pSNRP4, &pJSON_SNRP4, pError));

    // L = username
    ABC_BUF_DUP_PTR(pKeys->L, pKeys->szUserName, strlen(pKeys->szUserName));
    //ABC_UtilHexDumpBuf("L", pKeys->L);

    // L1 = Scrypt(L, SNRP1)
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP1, &(pKeys->L1), pError));
    //ABC_UtilHexDumpBuf("L1", pKeys->L1);

    // P = password
    ABC_BUF_DUP_PTR(pKeys->P, pKeys->szPassword, strlen(pKeys->szPassword));
    //ABC_UtilHexDumpBuf("P", pKeys->P);

    // LP = L + P
    ABC_BUF_DUP(pKeys->LP, pKeys->L);
    ABC_BUF_APPEND(pKeys->LP, pKeys->P);

    // LP1 = Scrypt(P, SNRP1)
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->LP, pKeys->pSNRP1, &(pKeys->LP1), pError));

    // CarePackage = ERQ, SNRP2, SNRP3, SNRP4
    ABC_CHECK_RET(ABC_LoginCreateCarePackageJSONString(NULL, pJSON_SNRP2, pJSON_SNRP3, pJSON_SNRP4, &szCarePackage_JSON, pError));

    // L4 = Scrypt(L + P, SNRP4)
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP4, &(pKeys->L4), pError));

    // LP2 = Scrypt(L + P, SNRP2)
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->LP, pKeys->pSNRP2, &(pKeys->LP2), pError));

    // find the next available account number on this device
    ABC_CHECK_RET(ABC_LoginDirNewNumber(&(pKeys->accountNum), pError));

    // create the main account directory
    ABC_ALLOC(szAccountDir, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyAccountDirName(szAccountDir, pKeys->accountNum, pError));
    ABC_CHECK_RET(ABC_FileIOCreateDir(szAccountDir, pError));

    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);

    // create the name file data and write the file
    ABC_CHECK_RET(ABC_UtilCreateValueJSONString(pKeys->szUserName, JSON_ACCT_USERNAME_FIELD, &szJSON, pError));
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_NAME_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szJSON, pError));
    ABC_FREE_STR(szJSON);
    szJSON = NULL;

    // Create MK
    ABC_CHECK_RET(ABC_CryptoCreateRandomData(ACCOUNT_MK_LENGTH, &MK, pError));
    ABC_BUF_DUP(pKeys->MK, MK);

    // Create Repo key
    ABC_CHECK_RET(ABC_CryptoCreateRandomData(SYNC_KEY_LENGTH, &RepoAcctKey, pError));
    ABC_CHECK_RET(ABC_CryptoHexEncode(RepoAcctKey, &(pKeys->szRepoAcctKey), pError));

    // Create LoginPackage json
    ABC_CHECK_RET(
        ABC_LoginUpdateLoginPackageJSONString(
            pKeys, MK, pKeys->szRepoAcctKey, NULL_BUF, NULL_BUF,
            &szLoginPackage_JSON, pError));

    // Create the repo and account on server
    ABC_CHECK_RET(ABC_LoginServerCreate(pKeys->L1, pKeys->LP1,
                    szCarePackage_JSON, szLoginPackage_JSON,
                    pKeys->szRepoAcctKey, pError));

    // write the file care package to a file
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_CARE_PACKAGE_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szCarePackage_JSON, pError));

    // write the file login package to a file
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_LOGIN_PACKAGE_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szLoginPackage_JSON, pError));

    ABC_CHECK_RET(ABC_LoginCreateSync(szAccountDir, pError));

    // we now have a new account so go ahead and cache it's keys
    ABC_CHECK_RET(ABC_LoginAddToKeyCache(pKeys, pError));

    // Populate the sync dir with files:
    ABC_CHECK_RET(ABC_LoginGetSyncKeys(szUserName, szPassword, &pSyncKeys, pError));
    ABC_CHECK_RET(ABC_AccountCreate(pSyncKeys, pError));

    // take this opportunity to download the questions they can choose from for recovery
    ABC_CHECK_RET(ABC_GeneralUpdateQuestionChoices(pError));

    // also take this non-blocking opportunity to update the info from the server if needed
    ABC_CHECK_RET(ABC_GeneralUpdateInfo(pError));

    // Load the general info
    ABC_CHECK_RET(ABC_GeneralGetInfo(&pGeneralInfo, pError));

    // Create
    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_SYNC_DIR);

    // Init the git repo and sync it
    int dirty;
    ABC_CHECK_RET(ABC_SyncMakeRepo(szFilename, pError));
    ABC_CHECK_RET(ABC_SyncRepo(szFilename, pKeys->szRepoAcctKey, &dirty, pError));

    ABC_CHECK_RET(ABC_LoginServerActivate(pKeys->L1, pKeys->LP1, pError));
    pKeys = NULL; // so we don't free what we just added to the cache
exit:
    if (cc != ABC_CC_Ok)
    {
        ABC_FileIODeleteRecursive(szAccountDir, NULL);
    }
    if (pKeys)
    {
        ABC_LoginFreeAccountKeys(pKeys);
        ABC_CLEAR_FREE(pKeys, sizeof(tAccountKeys));
    }
    if (pSyncKeys)          ABC_SyncFreeKeys(pSyncKeys);
    if (pJSON_SNRP2)        json_decref(pJSON_SNRP2);
    if (pJSON_SNRP3)        json_decref(pJSON_SNRP3);
    if (pJSON_SNRP4)        json_decref(pJSON_SNRP4);
    if (pJSON_EMK)          json_decref(pJSON_EMK);
    if (pJSON_ESyncKey)     json_decref(pJSON_ESyncKey);
    ABC_FREE_STR(szCarePackage_JSON);
    ABC_FREE_STR(szJSON);
    ABC_FREE_STR(szERepoAcctKey);
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szFilename);
    ABC_GeneralFreeInfo(pGeneralInfo);
    ABC_AccountSettingsFree(pSettings);

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Fetchs account from server. Only used by ABC_LoginSignIn.
 *
 * @param szUserName   Login
 * @param szPassword   Password
 */
static
tABC_CC ABC_LoginFetch(const char *szUserName, const char *szPassword, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    int dirty;
    char *szAccountDir      = NULL;
    char *szFilename        = NULL;
    char *szCarePackage     = NULL;
    char *szLoginPackage    = NULL;
    tAccountKeys *pKeys     = NULL;
    tABC_U08Buf L           = ABC_BUF_NULL;
    tABC_U08Buf L1          = ABC_BUF_NULL;
    tABC_U08Buf P           = ABC_BUF_NULL;
    tABC_U08Buf LP          = ABC_BUF_NULL;
    tABC_U08Buf LP1         = ABC_BUF_NULL;
    tABC_U08Buf NULL_LRA1   = ABC_BUF_NULL;
    tABC_CryptoSNRP *pSNRP1 = NULL;

    // Create L, P, SNRP1, L1, LP1
    ABC_BUF_DUP_PTR(L, szUserName, strlen(szUserName));
    ABC_BUF_DUP_PTR(P, szPassword, strlen(szPassword));
    ABC_CHECK_RET(ABC_CryptoCreateSNRPForServer(&pSNRP1, pError));
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(L, pSNRP1, &L1, pError));
    ABC_BUF_DUP(LP, L);
    ABC_BUF_APPEND(LP, P);
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(LP, pSNRP1, &LP1, pError));

    // Download CarePackage.json
    ABC_CHECK_RET(ABC_LoginServerGetCarePackage(L1, &szCarePackage, pError));

    // Download LoginPackage.json
    ABC_CHECK_RET(ABC_LoginServerGetLoginPackage(L1, LP1, NULL_LRA1, &szLoginPackage, pError));

    // Setup initial account directories and files
    ABC_CHECK_RET(
        ABC_LoginInitPackages(szUserName, L1,
                              szCarePackage, szLoginPackage,
                              &szAccountDir, pError));

    // We have the care package so fetch keys
    ABC_CHECK_RET(ABC_LoginCacheKeys(szUserName, szPassword, &pKeys, pError));

    //  Create sync directory and sync
    ABC_CHECK_RET(ABC_LoginCreateSync(szAccountDir, pError));

    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_SYNC_DIR);

    // Init the git repo and sync it
    ABC_CHECK_RET(ABC_SyncMakeRepo(szFilename, pError));
    ABC_CHECK_RET(ABC_SyncRepo(szFilename, pKeys->szRepoAcctKey, &dirty, pError));
exit:
    if (cc != ABC_CC_Ok)
    {
        ABC_FileIODeleteRecursive(szAccountDir, NULL);
        ABC_LoginClearKeyCache(NULL);
    }
    ABC_FREE_STR(szFilename);
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szCarePackage);
    ABC_FREE_STR(szLoginPackage);
    ABC_BUF_FREE(L);
    ABC_BUF_FREE(L1);
    ABC_BUF_FREE(P);
    ABC_BUF_FREE(LP);
    ABC_BUF_FREE(LP1);
    ABC_CryptoFreeSNRP(&pSNRP1);

    return cc;
}

/**
 * The account does not exist, so create and populate a directory.
 */
static
tABC_CC ABC_LoginInitPackages(const char *szUserName, tABC_U08Buf L1, const char *szCarePackage, const char *szLoginPackage, char **szAccountDir, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    char *szFilename    = NULL;
    char *szJSON        = NULL;
    int AccountNum      = 0;

    // find the next available account number on this device
    ABC_CHECK_RET(ABC_LoginDirNewNumber(&AccountNum, pError));

    // create the main account directory
    ABC_ALLOC(*szAccountDir, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyAccountDirName(*szAccountDir, AccountNum, pError));
    ABC_CHECK_RET(ABC_FileIOCreateDir(*szAccountDir, pError));

    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);

    // create the name file data and write the file
    ABC_CHECK_RET(ABC_UtilCreateValueJSONString(szUserName, JSON_ACCT_USERNAME_FIELD, &szJSON, pError));
    sprintf(szFilename, "%s/%s", *szAccountDir, ACCOUNT_NAME_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szJSON, pError));

    //  Save Care Package
    sprintf(szFilename, "%s/%s", *szAccountDir, ACCOUNT_CARE_PACKAGE_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szCarePackage, pError));

    // Save Login Package
    sprintf(szFilename, "%s/%s", *szAccountDir, ACCOUNT_LOGIN_PACKAGE_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szLoginPackage, pError));
exit:
    ABC_FREE_STR(szJSON);
    return cc;
}

/**
 * Only used by ABC_LoginCheckRecoveryAnswers.
 */
static
tABC_CC ABC_LoginRepoSetup(const tAccountKeys *pKeys, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    tABC_U08Buf L4          = ABC_BUF_NULL;
    tABC_U08Buf SyncKey     = ABC_BUF_NULL;
    char *szAccountDir      = NULL;
    char *szFilename        = NULL;
    char *szRepoAcctKey     = NULL;
    char *szREPO_JSON       = NULL;
    json_t *pJSON_ESyncKey  = NULL;

    ABC_CHECK_RET(ABC_LoginGetKey(pKeys->szUserName, NULL, ABC_LoginKey_L4, &L4, pError));
    ABC_CHECK_RET(
        ABC_LoginGetLoginPackageObjects(pKeys->accountNum, NULL, NULL,
                                        &pJSON_ESyncKey, NULL, NULL, pError));

    // Decrypt ESyncKey
    tABC_CC CC_Decrypt = ABC_CryptoDecryptJSONObject(pJSON_ESyncKey, L4, &SyncKey, pError);
    // check the results
    if (ABC_CC_DecryptFailure == CC_Decrypt)
    {
        ABC_RET_ERROR(ABC_CC_BadPassword, "Could not decrypt RepoAcctKey - bad password");
    }
    else if (ABC_CC_Ok != CC_Decrypt)
    {
        cc = CC_Decrypt;
        goto exit;
    }

    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_ALLOC(szAccountDir, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyAccountDirName(szAccountDir, pKeys->accountNum, pError));

    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_SYNC_DIR);

    //  Create sync directory and sync
    ABC_CHECK_RET(ABC_LoginCreateSync(szAccountDir, pError));
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_SYNC_DIR);

    // Create repo URL
    char *szSyncKey = (char *) ABC_BUF_PTR(SyncKey);

    // Init the git repo and sync it
    int dirty;
    ABC_CHECK_RET(ABC_SyncMakeRepo(szFilename, pError));
    ABC_CHECK_RET(ABC_SyncRepo(szFilename, szSyncKey, &dirty, pError));
exit:
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szFilename);
    ABC_FREE_STR(szREPO_JSON);
    ABC_FREE_STR(szRepoAcctKey);
    ABC_BUF_FREE(SyncKey);
    if (pJSON_ESyncKey) json_decref(pJSON_ESyncKey);
    return cc;
}

/**
 * Set the recovery questions for an account
 *
 * This function sets the password recovery informatin for the account.
 * This includes sending a new care package to the server
 *
 * @param pInfo     Pointer to recovery information data
 * @param pError    A pointer to the location to store the error if there is one
 */
tABC_CC ABC_LoginSetRecovery(const char *szUserName,
                             const char *szPassword,
                             const char *szRecoveryQuestions,
                             const char *szRecoveryAnswers,
                             tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    tAccountKeys    *pKeys               = NULL;
    json_t          *pJSON_ERQ           = NULL;
    json_t          *pJSON_SNRP2         = NULL;
    json_t          *pJSON_SNRP3         = NULL;
    json_t          *pJSON_SNRP4         = NULL;
    char            *szCarePackage_JSON  = NULL;
    char            *szLoginPackage_JSON = NULL;
    char            *szAccountDir        = NULL;
    char            *szFilename          = NULL;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));

    int AccountNum = 0;

    // check locally for the account
    ABC_CHECK_RET(ABC_LoginDirGetNumber(szUserName, &AccountNum, pError));
    if (AccountNum < 0)
    {
        ABC_RET_ERROR(ABC_CC_AccountDoesNotExist, "No account by that name");
    }

    // cache up the keys
    ABC_CHECK_RET(ABC_LoginCacheKeys(szUserName, szPassword, &pKeys, pError));

    // the following should all be available
    // szUserName, szPassword, L, P, LP2, SNRP2, SNRP3, SNRP4
    ABC_CHECK_ASSERT(NULL != ABC_BUF_PTR(pKeys->L), ABC_CC_Error, "Expected to find L in key cache");
    ABC_CHECK_ASSERT(NULL != ABC_BUF_PTR(pKeys->P), ABC_CC_Error, "Expected to find P in key cache");
    ABC_CHECK_ASSERT(NULL != ABC_BUF_PTR(pKeys->LP2), ABC_CC_Error, "Expected to find LP2 in key cache");
    ABC_CHECK_ASSERT(NULL != pKeys->pSNRP3, ABC_CC_Error, "Expected to find SNRP3 in key cache");
    ABC_CHECK_ASSERT(NULL != pKeys->pSNRP4, ABC_CC_Error, "Expected to find SNRP4 in key cache");

    // Create the keys that we still need or that need to be updated

    // SNRP1
    if (NULL == pKeys->pSNRP1)
    {
        ABC_CHECK_RET(ABC_CryptoCreateSNRPForServer(&(pKeys->pSNRP1), pError));
    }

    // LRA = L + RA
    if (ABC_BUF_PTR(pKeys->LRA) != NULL)
    {
        ABC_BUF_FREE(pKeys->LRA);
    }
    ABC_BUF_DUP(pKeys->LRA, pKeys->L);
    ABC_BUF_APPEND_PTR(pKeys->LRA, szRecoveryAnswers, strlen(szRecoveryAnswers));

    // LRA1 = Scrypt(L + RA, SNRP1)
    if (ABC_BUF_PTR(pKeys->LRA1) != NULL)
    {
        ABC_BUF_FREE(pKeys->LRA1);
    }
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->LRA, pKeys->pSNRP1, &(pKeys->LRA1), pError));


    // LRA3 = Scrypt(L + RA, SNRP3)
    if (ABC_BUF_PTR(pKeys->LRA3) != NULL)
    {
        ABC_BUF_FREE(pKeys->LRA3);
    }
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->LRA, pKeys->pSNRP3, &(pKeys->LRA3), pError));

    // L4 = Scrypt(L, SNRP4)
    if (ABC_BUF_PTR(pKeys->L4) == NULL)
    {
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP4, &(pKeys->L4), pError));
    }

    // RQ
    if (ABC_BUF_PTR(pKeys->RQ) != NULL)
    {
        ABC_BUF_FREE(pKeys->RQ);
    }
    ABC_BUF_DUP_PTR(pKeys->RQ, szRecoveryQuestions, strlen(szRecoveryQuestions) + 1);

    // L1 = Scrypt(L, SNRP1)
    if (ABC_BUF_PTR(pKeys->L1) == NULL)
    {
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP1, &(pKeys->L1), pError));
    }

    // LP1 = Scrypt(LP, SNRP1)
    if (ABC_BUF_PTR(pKeys->LP1) == NULL)
    {
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->LP, pKeys->pSNRP1, &(pKeys->LP1), pError));
    }

    // Create LoginPackage json
    ABC_CHECK_RET(
        ABC_LoginUpdateLoginPackageJSONString(
            pKeys, pKeys->MK, pKeys->szRepoAcctKey, pKeys->LP2, pKeys->LRA3,
            &szLoginPackage_JSON, pError));

    // ERQ = AES256(RQ, L4)
    ABC_CHECK_RET(ABC_CryptoEncryptJSONObject(
                         pKeys->RQ, pKeys->L4, ABC_CryptoType_AES256,
                         &pJSON_ERQ, pError));

    // update the care package
    // get the current care package
    ABC_CHECK_RET(ABC_LoginGetCarePackageObjects(
                    AccountNum, NULL, NULL,
                    &pJSON_SNRP2, &pJSON_SNRP3, &pJSON_SNRP4, pError));

    // Create an updated CarePackage = ERQ, SNRP2, SNRP3, SNRP4
    ABC_CHECK_RET(ABC_LoginCreateCarePackageJSONString(
                    pJSON_ERQ, pJSON_SNRP2, pJSON_SNRP3,
                    pJSON_SNRP4, &szCarePackage_JSON, pError));

    // Client sends L1, LP1, LRA1, CarePackage, to the server
    ABC_CHECK_RET(
        ABC_LoginServerSetRecovery(pKeys->L1, pKeys->LP1, pKeys->LRA1,
                                   szCarePackage_JSON, szLoginPackage_JSON,
                                   pError));

    // create the main account directory
    ABC_ALLOC(szAccountDir, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyAccountDirName(szAccountDir, pKeys->accountNum, pError));

    // write the file care package to a file
    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_CARE_PACKAGE_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szCarePackage_JSON, pError));

    // update login package
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_LOGIN_PACKAGE_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szLoginPackage_JSON, pError));

    int dirty;
    ABC_CHECK_RET(ABC_LoginSyncData(pKeys->szUserName, pKeys->szPassword, &dirty, pError));
exit:
    if (pJSON_ERQ)          json_decref(pJSON_ERQ);
    if (pJSON_SNRP2)        json_decref(pJSON_SNRP2);
    if (pJSON_SNRP3)        json_decref(pJSON_SNRP3);
    if (pJSON_SNRP4)        json_decref(pJSON_SNRP4);
    ABC_FREE_STR(szCarePackage_JSON);
    ABC_FREE_STR(szLoginPackage_JSON);
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szFilename);

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Change password for an account
 *
 * This function sets the password recovery informatin for the account.
 * This includes sending a new care package to the server
 *
 * @param pInfo     Pointer to password change information data
 * @param pError    A pointer to the location to store the error if there is one
 */
tABC_CC ABC_LoginChangePassword(const char *szUserName,
                                const char *szPassword,
                                const char *szRecoveryAnswers,
                                const char *szNewPassword,
                                tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    tAccountKeys *pKeys = NULL;
    tABC_U08Buf oldLP2       = ABC_BUF_NULL;
    tABC_U08Buf LRA3         = ABC_BUF_NULL;
    tABC_U08Buf LRA          = ABC_BUF_NULL;
    tABC_U08Buf LRA1         = ABC_BUF_NULL;
    tABC_U08Buf oldLP1       = ABC_BUF_NULL;
    tABC_U08Buf MK           = ABC_BUF_NULL;
    char *szAccountDir        = NULL;
    char *szFilename          = NULL;
    char *szJSON              = NULL;
    char *szLoginPackage_JSON = NULL;
    json_t *pJSON_ELP2 = NULL;
    json_t *pJSON_MK   = NULL;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));

    // get the account directory and set up for creating needed filenames
    ABC_CHECK_RET(ABC_LoginGetDirName(szUserName, &szAccountDir, pError));
    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);

    // get the keys for this user (note: password can be NULL for this call)
    ABC_CHECK_RET(ABC_LoginCacheKeys(szUserName, szPassword, &pKeys, pError));

    // we need to obtain the original LP2 and LRA3
    if (szPassword != NULL)
    {
        // we had the password so we should have the LP2 key
        ABC_CHECK_ASSERT(NULL != ABC_BUF_PTR(pKeys->LP2), ABC_CC_Error, "Expected to find LP2 in key cache");
        ABC_BUF_DUP(oldLP2, pKeys->LP2);
        ABC_BUF_DUP(LRA3, pKeys->LRA3);

        // create the old LP1 for use in server auth -> LP1 = Scrypt(LP, SNRP1)
        tABC_U08Buf LP1 = ABC_BUF_NULL;
        ABC_BUF_DUP(LP1, pKeys->L);
        ABC_BUF_APPEND(LP1, pKeys->P);


        ABC_CHECK_RET(ABC_CryptoScryptSNRP(LP1, pKeys->pSNRP1, &oldLP1, pError));
    }
    else
    {
        // we have the recovery questions so we can make the LRA3

        // LRA = L + RA
        ABC_BUF_DUP(LRA, pKeys->L);
        ABC_BUF_APPEND_PTR(LRA, szRecoveryAnswers, strlen(szRecoveryAnswers));

        // LRA3 = Scrypt(LRA, SNRP3)
        ABC_CHECK_ASSERT(NULL != pKeys->pSNRP3, ABC_CC_Error, "Expected to find SNRP3 in key cache");
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(LRA, pKeys->pSNRP3, &LRA3, pError));

        ABC_CHECK_RET(
            ABC_LoginGetLoginPackageObjects(
                pKeys->accountNum, NULL,
                &pJSON_MK, NULL, &pJSON_ELP2, NULL, pError));

        // get the LP2 by decrypting ELP2 with LRA3
        ABC_CHECK_RET(ABC_CryptoDecryptJSONObject(pJSON_ELP2, LRA3, &oldLP2, pError));
        // Get MK from old LP2
        ABC_CHECK_RET(ABC_CryptoDecryptJSONObject(pJSON_MK, oldLP2, &MK, pError));

        // set the MK
        ABC_BUF_DUP(pKeys->MK, MK);

        // create LRA1 as it will be needed for server communication later
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(LRA, pKeys->pSNRP1, &LRA1, pError));
    }

    // we now have oldLP2 and oldLRA3

    // time to set the new data for this account

    // set new password
    ABC_FREE_STR(pKeys->szPassword);
    ABC_STRDUP(pKeys->szPassword, szNewPassword);

    // set new P
    ABC_BUF_FREE(pKeys->P);
    ABC_BUF_DUP_PTR(pKeys->P, pKeys->szPassword, strlen(pKeys->szPassword));

    ABC_BUF_FREE(pKeys->LP);
    ABC_BUF_DUP(pKeys->LP, pKeys->L);
    ABC_BUF_APPEND(pKeys->LP, pKeys->P);

    // set new LP1
    ABC_BUF_FREE(pKeys->LP1);
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->LP, pKeys->pSNRP1, &(pKeys->LP1), pError));

    // set new LP = L + P
    ABC_BUF_FREE(pKeys->LP);
    ABC_BUF_DUP(pKeys->LP, pKeys->L);
    ABC_BUF_APPEND(pKeys->LP, pKeys->P);

    // set new LP2 = Scrypt(L + P, SNRP2)
    ABC_BUF_FREE(pKeys->LP2);
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->LP, pKeys->pSNRP2, &(pKeys->LP2), pError));

    // we'll need L1 for server communication L1 = Scrypt(L, SNRP1)
    if (ABC_BUF_PTR(pKeys->L1) == NULL)
    {
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP1, &(pKeys->L1), pError));
    }

    // Update Login Package
    ABC_CHECK_RET(ABC_LoginUpdateLoginPackageJSONString(pKeys, pKeys->MK, pKeys->szRepoAcctKey, pKeys->LP2, LRA3, &szLoginPackage_JSON, pError));

    // server change password - Server will need L1, (P1 or LRA1) and new_P1
    ABC_CHECK_RET(ABC_LoginServerChangePassword(pKeys->L1, oldLP1, LRA1, pKeys->LP1, szLoginPackage_JSON, pError));

    // Write the new Login Package
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_LOGIN_PACKAGE_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szLoginPackage_JSON, pError));

    // Clear wallet cache
    ABC_CHECK_RET(ABC_WalletClearCache(pError));

    // Sync the data (ELP2 and ELRA3) with server
    int dirty;
    ABC_CHECK_RET(ABC_LoginSyncData(szUserName, szNewPassword, &dirty, pError));
exit:
    ABC_BUF_FREE(oldLP2);
    ABC_BUF_FREE(LRA3);
    ABC_BUF_FREE(LRA);
    ABC_BUF_FREE(LRA1);
    ABC_BUF_FREE(oldLP1);
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szFilename);
    ABC_FREE_STR(szJSON);
    ABC_FREE_STR(szLoginPackage_JSON);
    if (pJSON_ELP2) json_decref(pJSON_ELP2);
    if (pJSON_MK) json_decref(pJSON_MK);
    if (cc != ABC_CC_Ok) ABC_LoginClearKeyCache(NULL);

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Creates the JSON care package
 *
 * @param pJSON_ERQ    Pointer to ERQ JSON object
 *                     (if this is NULL, ERQ is not added to the care package)
 * @param pJSON_SNRP2  Pointer to SNRP2 JSON object
 * @param pJSON_SNRP3  Pointer to SNRP3 JSON object
 * @param pJSON_SNRP4  Pointer to SNRIP4 JSON object
 * @param pszJSON      Pointer to store allocated JSON for care package.
 *                     (the user is responsible for free'ing this pointer)
 */
static
tABC_CC ABC_LoginCreateCarePackageJSONString(const json_t *pJSON_ERQ,
                                               const json_t *pJSON_SNRP2,
                                               const json_t *pJSON_SNRP3,
                                               const json_t *pJSON_SNRP4,
                                               char         **pszJSON,
                                               tABC_Error   *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    json_t *pJSON_Root = NULL;

    ABC_CHECK_NULL(pJSON_SNRP2);
    ABC_CHECK_NULL(pJSON_SNRP3);
    ABC_CHECK_NULL(pJSON_SNRP4);
    ABC_CHECK_NULL(pszJSON);

    pJSON_Root = json_object();

    if (pJSON_ERQ != NULL)
    {
        json_object_set(pJSON_Root, JSON_ACCT_ERQ_FIELD, (json_t *) pJSON_ERQ);
    }
    json_object_set(pJSON_Root, JSON_ACCT_SNRP2_FIELD, (json_t *) pJSON_SNRP2);
    json_object_set(pJSON_Root, JSON_ACCT_SNRP3_FIELD, (json_t *) pJSON_SNRP3);
    json_object_set(pJSON_Root, JSON_ACCT_SNRP4_FIELD, (json_t *) pJSON_SNRP4);

    *pszJSON = ABC_UtilStringFromJSONObject(pJSON_Root, JSON_INDENT(4) | JSON_PRESERVE_ORDER);

exit:
    if (pJSON_Root) json_decref(pJSON_Root);

    return cc;
}

/**
 * Creates the JSON login package
 *
 * @param pJSON_MK           Pointer to MK JSON object
 * @param pJSON_SyncKey      Pointer to Repo Acct Key JSON object
 * @param pszJSON       Pointer to store allocated JSON for care package.
 *                     (the user is responsible for free'ing this pointer)
 */
static
tABC_CC ABC_LoginCreateLoginPackageJSONString(const json_t *pJSON_MK,
                                              const json_t *pJSON_SyncKey,
                                              const json_t *pJSON_ELP2,
                                              const json_t *pJSON_ELRA3,
                                              char         **pszJSON,
                                              tABC_Error   *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    json_t *pJSON_Root = NULL;

    ABC_CHECK_NULL(pJSON_MK);
    ABC_CHECK_NULL(pJSON_SyncKey);
    ABC_CHECK_NULL(pszJSON);

    pJSON_Root = json_object();

    json_object_set(pJSON_Root, JSON_ACCT_MK_FIELD, (json_t *) pJSON_MK);
    json_object_set(pJSON_Root, JSON_ACCT_SYNCKEY_FIELD, (json_t *) pJSON_SyncKey);
    if (pJSON_ELP2)
    {
        json_object_set(pJSON_Root, JSON_ACCT_ELP2_FIELD, (json_t *) pJSON_ELP2);
    }
    if (pJSON_ELRA3)
    {
        json_object_set(pJSON_Root, JSON_ACCT_ELRA3_FIELD, (json_t *) pJSON_ELRA3);
    }
    *pszJSON = ABC_UtilStringFromJSONObject(pJSON_Root, JSON_INDENT(4) | JSON_PRESERVE_ORDER);
exit:
    if (pJSON_Root) json_decref(pJSON_Root);

    return cc;
}

static
tABC_CC ABC_LoginUpdateLoginPackageJSONString(tAccountKeys *pKeys,
                                              tABC_U08Buf MK,
                                              const char *szRepoAcctKey,
                                              tABC_U08Buf LP2,
                                              tABC_U08Buf LRA3,
                                              char **szLoginPackage,
                                              tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    bool bExists = false;
    char *szAccountDir = NULL;
    char *szFilename   = NULL;
    json_t *pJSON_EMK            = NULL;
    json_t *pJSON_ESyncKey       = NULL;
    json_t *pJSON_ELP2           = NULL;
    json_t *pJSON_ELRA3          = NULL;

    ABC_CHECK_NULL(szLoginPackage);
    ABC_CHECK_NULL(szRepoAcctKey);

    // get the main account directory
    ABC_ALLOC(szAccountDir, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyAccountDirName(szAccountDir, pKeys->accountNum, pError));

    // create the name of the care package file
    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_LOGIN_PACKAGE_FILENAME);

    ABC_CHECK_RET(ABC_FileIOFileExists(szFilename, &bExists, pError));
    if (bExists)
    {
        ABC_CHECK_RET(ABC_LoginGetLoginPackageObjects(pKeys->accountNum, NULL,
                                            &pJSON_EMK,
                                            &pJSON_ESyncKey,
                                            &pJSON_ELP2,
                                            &pJSON_ELRA3,
                                            pError));
    }

    if (ABC_BUF_PTR(MK) != NULL)
    {
        if (pJSON_EMK) json_decref(pJSON_EMK);
        ABC_CHECK_RET(ABC_CryptoEncryptJSONObject(MK, pKeys->LP2, ABC_CryptoType_AES256, &pJSON_EMK, pError));
    }
    if (szRepoAcctKey)
    {
        if (pJSON_ESyncKey) json_decref(pJSON_ESyncKey);

        tABC_U08Buf RepoBuf = ABC_BUF_NULL;
        ABC_BUF_SET_PTR(RepoBuf, (unsigned char *)szRepoAcctKey, strlen(szRepoAcctKey) + 1);

        ABC_CHECK_RET(ABC_CryptoEncryptJSONObject(RepoBuf, pKeys->L4, ABC_CryptoType_AES256, &pJSON_ESyncKey, pError));
    }
    if (ABC_BUF_PTR(LP2) != NULL && ABC_BUF_PTR(LRA3) != NULL)
    {
        if (pJSON_ELP2) json_decref(pJSON_ELP2);
        if (pJSON_ELRA3) json_decref(pJSON_ELRA3);

        // write out new ELP2.json <- LP2 encrypted with recovery key (LRA3)
        ABC_CHECK_RET(ABC_CryptoEncryptJSONObject(LP2, LRA3, ABC_CryptoType_AES256, &pJSON_ELP2, pError));

        // write out new ELRA3.json <- LRA3 encrypted with LP2 (L+P,S2)
        ABC_CHECK_RET(ABC_CryptoEncryptJSONObject(LRA3, LP2, ABC_CryptoType_AES256, &pJSON_ELRA3, pError));
    }

    ABC_CHECK_RET(
        ABC_LoginCreateLoginPackageJSONString(pJSON_EMK,
                                              pJSON_ESyncKey,
                                              pJSON_ELP2,
                                              pJSON_ELRA3,
                                              szLoginPackage,
                                              pError));
exit:
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szFilename);

    if (pJSON_EMK)      json_decref(pJSON_EMK);
    if (pJSON_ESyncKey) json_decref(pJSON_ESyncKey);
    if (pJSON_ELP2)     json_decref(pJSON_ELP2);
    if (pJSON_ELRA3)    json_decref(pJSON_ELRA3);

    return cc;
}

// loads the json care package for a given account number
// if the ERQ doesn't exist, ppJSON_ERQ is set to NULL

/**
 * Loads the json care package for a given account number
 *
 * The JSON objects for each argument will be assigned.
 * The function assumes any number of the arguments may be NULL,
 * in which case, they are not set.
 * It is also possible that there is no recovery questions, in which case
 * the ERQ will be set to NULL.
 *
 * @param AccountNum   Account number of the account of interest
 * @param ppJSON_ERQ   Pointer store ERQ JSON object (can be NULL) - caller expected to decref
 * @param ppJSON_SNRP2 Pointer store SNRP2 JSON object (can be NULL) - caller expected to decref
 * @param ppJSON_SNRP3 Pointer store SNRP3 JSON object (can be NULL) - caller expected to decref
 * @param ppJSON_SNRP4 Pointer store SNRP4 JSON object (can be NULL) - caller expected to decref
 * @param pError       A pointer to the location to store the error if there is one
 */
static
tABC_CC ABC_LoginGetCarePackageObjects(int          AccountNum,
                                         const char   *szCarePackage,
                                         json_t       **ppJSON_ERQ,
                                         json_t       **ppJSON_SNRP2,
                                         json_t       **ppJSON_SNRP3,
                                         json_t       **ppJSON_SNRP4,
                                         tABC_Error   *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szAccountDir = NULL;
    char *szCarePackageFilename = NULL;
    char *szCarePackage_JSON = NULL;
    json_t *pJSON_Root = NULL;
    json_t *pJSON_ERQ = NULL;
    json_t *pJSON_SNRP2 = NULL;
    json_t *pJSON_SNRP3 = NULL;
    json_t *pJSON_SNRP4 = NULL;

    // if we supply a care package, use it instead
    if (szCarePackage)
    {
        ABC_STRDUP(szCarePackage_JSON, szCarePackage);
    }
    else
    {
        ABC_CHECK_ASSERT(AccountNum >= 0, ABC_CC_AccountDoesNotExist, "Bad account number");

        // get the main account directory
        ABC_ALLOC(szAccountDir, ABC_FILEIO_MAX_PATH_LENGTH);
        ABC_CHECK_RET(ABC_LoginCopyAccountDirName(szAccountDir, AccountNum, pError));

        // create the name of the care package file
        ABC_ALLOC(szCarePackageFilename, ABC_FILEIO_MAX_PATH_LENGTH);
        sprintf(szCarePackageFilename, "%s/%s", szAccountDir, ACCOUNT_CARE_PACKAGE_FILENAME);

        // load the care package
        ABC_CHECK_RET(ABC_FileIOReadFileStr(szCarePackageFilename, &szCarePackage_JSON, pError));
    }

    // decode the json
    json_error_t error;
    pJSON_Root = json_loads(szCarePackage_JSON, 0, &error);
    ABC_CHECK_ASSERT(pJSON_Root != NULL, ABC_CC_JSONError, "Error parsing JSON care package");
    ABC_CHECK_ASSERT(json_is_object(pJSON_Root), ABC_CC_JSONError, "Error parsing JSON care package");

    // get the ERQ
    pJSON_ERQ = json_object_get(pJSON_Root, JSON_ACCT_ERQ_FIELD);
    //ABC_CHECK_ASSERT((pJSON_ERQ && json_is_object(pJSON_ERQ)), ABC_CC_JSONError, "Error parsing JSON care package - missing ERQ");

    // get SNRP2
    pJSON_SNRP2 = json_object_get(pJSON_Root, JSON_ACCT_SNRP2_FIELD);
    ABC_CHECK_ASSERT((pJSON_SNRP2 && json_is_object(pJSON_SNRP2)), ABC_CC_JSONError, "Error parsing JSON care package - missing SNRP2");

    // get SNRP3
    pJSON_SNRP3 = json_object_get(pJSON_Root, JSON_ACCT_SNRP3_FIELD);
    ABC_CHECK_ASSERT((pJSON_SNRP3 && json_is_object(pJSON_SNRP3)), ABC_CC_JSONError, "Error parsing JSON care package - missing SNRP3");

    // get SNRP4
    pJSON_SNRP4 = json_object_get(pJSON_Root, JSON_ACCT_SNRP4_FIELD);
    ABC_CHECK_ASSERT((pJSON_SNRP4 && json_is_object(pJSON_SNRP4)), ABC_CC_JSONError, "Error parsing JSON care package - missing SNRP4");

    // assign what we found (we need to increment the refs because they were borrowed from root)
    if (ppJSON_ERQ)
    {
        if (pJSON_ERQ)
        {
            *ppJSON_ERQ = json_incref(pJSON_ERQ);
        }
        else
        {
            *ppJSON_ERQ = NULL;
        }
    }
    if (ppJSON_SNRP2)
    {
        *ppJSON_SNRP2 = json_incref(pJSON_SNRP2);
    }
    if (ppJSON_SNRP3)
    {
        *ppJSON_SNRP3 = json_incref(pJSON_SNRP3);
    }
    if (ppJSON_SNRP4)
    {
        *ppJSON_SNRP4 = json_incref(pJSON_SNRP4);
    }

exit:
    if (pJSON_Root)             json_decref(pJSON_Root);
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szCarePackageFilename);
    ABC_FREE_STR(szCarePackage_JSON);

    return cc;
}

/**
 * Loads the json login package for a given account number
 *
 * The JSON objects for each argument will be assigned.
 * The function assumes any number of the arguments may be NULL,
 * in which case, they are not set.
 *
 * @param AccountNum      Account number of the account of interest
 * @param ppJSON_EMK      Pointer store EMK JSON object (can be NULL) - caller expected to decref
 * @param ppJSON_ESyncKey Pointer store ESyncKey JSON object (can be NULL) - caller expected to decref
 * @param ppJSON_ELP2     Pointer store ELP2 JSON object (can be NULL) - caller expected to decref
 * @param ppJSON_ELRA3    Pointer store ELRA JSON object (can be NULL) - caller expected to decref
 * @param pError          A pointer to the location to store the error if there is one
 */
static
tABC_CC ABC_LoginGetLoginPackageObjects(int          AccountNum,
                                        const char   *szLoginPackage,
                                        json_t       **ppJSON_EMK,
                                        json_t       **ppJSON_ESyncKey,
                                        json_t       **ppJSON_ELP2,
                                        json_t       **ppJSON_ELRA3,
                                        tABC_Error   *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    char *szAccountDir           = NULL;
    char *szLoginPackageFilename = NULL;
    char *szLoginPackage_JSON    = NULL;
    json_t *pJSON_Root           = NULL;
    json_t *pJSON_EMK            = NULL;
    json_t *pJSON_ESyncKey       = NULL;
    json_t *pJSON_ELP2           = NULL;
    json_t *pJSON_ELRA3          = NULL;

    // if we supply a care package, use it instead
    if (szLoginPackage)
    {
        ABC_STRDUP(szLoginPackage_JSON, szLoginPackage);
    }
    else
    {
        ABC_CHECK_ASSERT(AccountNum >= 0, ABC_CC_AccountDoesNotExist, "Bad account number");

        // get the main account directory
        ABC_ALLOC(szAccountDir, ABC_FILEIO_MAX_PATH_LENGTH);
        ABC_CHECK_RET(ABC_LoginCopyAccountDirName(szAccountDir, AccountNum, pError));

        // create the name of the care package file
        ABC_ALLOC(szLoginPackageFilename, ABC_FILEIO_MAX_PATH_LENGTH);
        sprintf(szLoginPackageFilename, "%s/%s", szAccountDir, ACCOUNT_LOGIN_PACKAGE_FILENAME);

        // load the care package
        ABC_CHECK_RET(ABC_FileIOReadFileStr(szLoginPackageFilename, &szLoginPackage_JSON, pError));
    }

    // decode the json
    json_error_t error;
    pJSON_Root = json_loads(szLoginPackage_JSON, 0, &error);
    ABC_CHECK_ASSERT(pJSON_Root != NULL, ABC_CC_JSONError, "Error parsing JSON login package");
    ABC_CHECK_ASSERT(json_is_object(pJSON_Root), ABC_CC_JSONError, "Error parsing JSON login package");

    // get the EMK
    pJSON_EMK = json_object_get(pJSON_Root, JSON_ACCT_MK_FIELD);
    ABC_CHECK_ASSERT((pJSON_EMK && json_is_object(pJSON_EMK)), ABC_CC_JSONError, "Error parsing JSON care package - missing ERQ");

    // get ESyncKey
    pJSON_ESyncKey = json_object_get(pJSON_Root, JSON_ACCT_SYNCKEY_FIELD);
    ABC_CHECK_ASSERT((pJSON_ESyncKey && json_is_object(pJSON_ESyncKey)), ABC_CC_JSONError, "Error parsing JSON care package - missing SNRP2");

    // get LP2
    pJSON_ELP2 = json_object_get(pJSON_Root, JSON_ACCT_ELP2_FIELD);
    if (pJSON_ELP2)
    {
        ABC_CHECK_ASSERT((pJSON_ELP2 && json_is_object(pJSON_ELP2)), ABC_CC_JSONError, "Error parsing JSON care package - missing LP2");
    }
    // get LRA3
    pJSON_ELRA3 = json_object_get(pJSON_Root, JSON_ACCT_ELRA3_FIELD);
    if (pJSON_ELRA3)
    {
        ABC_CHECK_ASSERT((pJSON_ELRA3 && json_is_object(pJSON_ELRA3)), ABC_CC_JSONError, "Error parsing JSON care package - missing LRA3");
    }

    if (ppJSON_EMK)
    {
        *ppJSON_EMK = json_incref(pJSON_EMK);
    }
    if (ppJSON_ESyncKey)
    {
        *ppJSON_ESyncKey = json_incref(pJSON_ESyncKey);
    }
    if (ppJSON_ELP2)
    {
        *ppJSON_ELP2 = json_incref(pJSON_ELP2);
    }
    if (ppJSON_ELRA3)
    {
        *ppJSON_ELRA3 = json_incref(pJSON_ELRA3);
    }
exit:
    if (pJSON_Root) json_decref(pJSON_Root);
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szLoginPackageFilename);
    ABC_FREE_STR(szLoginPackage_JSON);

    return cc;
}

tABC_CC ABC_LoginUpdateLoginPackageFromServer(const char *szUserName, const char *szPassword, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    tAccountKeys *pKeys  = NULL;

    ABC_CHECK_RET(ABC_LoginCacheKeys(szUserName, szPassword, &pKeys, pError));
    ABC_CHECK_RET(ABC_LoginGetKey(szUserName, szPassword, ABC_LoginKey_LP1, &(pKeys->LP1), pError));
    ABC_CHECK_RET(ABC_LoginUpdateLoginPackageFromServerBuf(pKeys->accountNum, pKeys->L1, pKeys->LP1, pError));
exit:
    return cc;
}

static
tABC_CC ABC_LoginUpdateLoginPackageFromServerBuf(int AccountNum, tABC_U08Buf L1, tABC_U08Buf LP1, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    char *szAccountDir   = NULL;
    char *szFilename     = NULL;
    char *szLoginPackage = NULL;
    tABC_U08Buf LPA_NULL = ABC_BUF_NULL;

    ABC_CHECK_RET(ABC_LoginServerGetLoginPackage(L1, LP1, LPA_NULL, &szLoginPackage, pError));

    ABC_ALLOC(szAccountDir, ABC_FILEIO_MAX_PATH_LENGTH);
    ABC_CHECK_RET(ABC_LoginCopyAccountDirName(szAccountDir, AccountNum, pError));

    ABC_ALLOC(szFilename, ABC_FILEIO_MAX_PATH_LENGTH);
    sprintf(szFilename, "%s/%s", szAccountDir, ACCOUNT_LOGIN_PACKAGE_FILENAME);
    ABC_CHECK_RET(ABC_FileIOWriteFileStr(szFilename, szLoginPackage, pError));
exit:
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szFilename);
    ABC_FREE_STR(szLoginPackage);
    return cc;
}

/**
 * Clears all the keys from the cache
 */
tABC_CC ABC_LoginClearKeyCache(tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));

    if ((gAccountKeysCacheCount > 0) && (NULL != gaAccountKeysCacheArray))
    {
        for (int i = 0; i < gAccountKeysCacheCount; i++)
        {
            tAccountKeys *pAccountKeys = gaAccountKeysCacheArray[i];
            ABC_LoginFreeAccountKeys(pAccountKeys);
        }

        ABC_FREE(gaAccountKeysCacheArray);
        gAccountKeysCacheCount = 0;
    }

exit:

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Frees all the elements in the given AccountKeys struct
 */
static void ABC_LoginFreeAccountKeys(tAccountKeys *pAccountKeys)
{
    if (pAccountKeys)
    {
        ABC_FREE_STR(pAccountKeys->szUserName);

        ABC_FREE_STR(pAccountKeys->szPassword);

        ABC_FREE_STR(pAccountKeys->szRepoAcctKey);

        ABC_CryptoFreeSNRP(&(pAccountKeys->pSNRP1));
        ABC_CryptoFreeSNRP(&(pAccountKeys->pSNRP2));
        ABC_CryptoFreeSNRP(&(pAccountKeys->pSNRP3));
        ABC_CryptoFreeSNRP(&(pAccountKeys->pSNRP4));

        ABC_BUF_FREE(pAccountKeys->L);
        ABC_BUF_FREE(pAccountKeys->L1);
        ABC_BUF_FREE(pAccountKeys->P);
        ABC_BUF_FREE(pAccountKeys->LP1);
        ABC_BUF_FREE(pAccountKeys->LRA);
        ABC_BUF_FREE(pAccountKeys->LRA1);
        ABC_BUF_FREE(pAccountKeys->L4);
        ABC_BUF_FREE(pAccountKeys->RQ);
        ABC_BUF_FREE(pAccountKeys->LP);
        ABC_BUF_FREE(pAccountKeys->LP2);
        ABC_BUF_FREE(pAccountKeys->LRA3);
    }
}

/**
 * Adds the given AccountKey to the array of cached account keys
 */
static tABC_CC ABC_LoginAddToKeyCache(tAccountKeys *pAccountKeys, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(pAccountKeys);

    // see if it exists first
    tAccountKeys *pExistingAccountKeys = NULL;
    ABC_CHECK_RET(ABC_LoginKeyFromCacheByName(pAccountKeys->szUserName, &pExistingAccountKeys, pError));

    // if it doesn't currently exist in the array
    if (pExistingAccountKeys == NULL)
    {
        // if we don't have an array yet
        if ((gAccountKeysCacheCount == 0) || (NULL == gaAccountKeysCacheArray))
        {
            // create a new one
            gAccountKeysCacheCount = 0;
            ABC_ALLOC(gaAccountKeysCacheArray, sizeof(tAccountKeys *));
        }
        else
        {
            // extend the current one
            gaAccountKeysCacheArray = realloc(gaAccountKeysCacheArray, sizeof(tAccountKeys *) * (gAccountKeysCacheCount + 1));

        }
        gaAccountKeysCacheArray[gAccountKeysCacheCount] = pAccountKeys;
        gAccountKeysCacheCount++;
    }
    else
    {
        cc = ABC_CC_AccountAlreadyExists;
    }

exit:

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Searches for a key in the cached by account name
 * if it is not found, the account keys will be set to NULL
 */
static tABC_CC ABC_LoginKeyFromCacheByName(const char *szUserName, tAccountKeys **ppAccountKeys, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(ppAccountKeys);

    // assume we didn't find it
    *ppAccountKeys = NULL;

    if ((gAccountKeysCacheCount > 0) && (NULL != gaAccountKeysCacheArray))
    {
        for (int i = 0; i < gAccountKeysCacheCount; i++)
        {
            tAccountKeys *pAccountKeys = gaAccountKeysCacheArray[i];
            if (0 == strcmp(szUserName, pAccountKeys->szUserName))
            {
                // found it
                *ppAccountKeys = pAccountKeys;
                break;
            }
        }
    }

exit:

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Adds the given user to the key cache if it isn't already cached.
 * With or without a password, szUserName, L, SNRP1, SNRP2, SNRP3, SNRP4 keys are retrieved and added if they aren't already in the cache
 * If a password is given, szPassword, P, LP2 keys are retrieved and the entry is added
 *  (the initial keys are added so the password can be verified while trying to
 *  decrypt the settings files)
 * If a pointer to hold the keys is given, then it is set to those keys
 */
static
tABC_CC ABC_LoginCacheKeys(const char *szUserName, const char *szPassword, tAccountKeys **ppKeys, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    tAccountKeys *pKeys              = NULL;
    tAccountKeys *pFinalKeys         = NULL;
    json_t       *pJSON_SNRP2        = NULL;
    json_t       *pJSON_SNRP3        = NULL;
    json_t       *pJSON_SNRP4        = NULL;
    json_t       *pJSON_EMK          = NULL;
    json_t       *pJSON_ESyncKey     = NULL;
    json_t       *pJSON_ELRA3        = NULL;
    tABC_U08Buf  REPO_JSON           = ABC_BUF_NULL;
    tABC_U08Buf  MK                  = ABC_BUF_NULL;
    json_t       *pJSON_Root         = NULL;
    tABC_U08Buf  P                   = ABC_BUF_NULL;
    tABC_U08Buf  LP                  = ABC_BUF_NULL;
    tABC_U08Buf  LP2                 = ABC_BUF_NULL;
    tABC_U08Buf  LRA3                = ABC_BUF_NULL;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(szUserName);

    // see if it is already in the cache
    ABC_CHECK_RET(ABC_LoginKeyFromCacheByName(szUserName, &pFinalKeys, pError));

    // if there wasn't an entry already in the cache - let's add it
    if (NULL == pFinalKeys)
    {
        // we need to add it but start with only those things that require the user name

        // check if the account exists
        int AccountNum = -1;
        ABC_CHECK_RET(ABC_LoginDirGetNumber(szUserName, &AccountNum, pError));
        if (AccountNum >= 0)
        {
            ABC_ALLOC(pKeys, sizeof(tAccountKeys));
            pKeys->accountNum = AccountNum;
            ABC_STRDUP(pKeys->szUserName, szUserName);
            pKeys->szPassword = NULL;

            ABC_CHECK_RET(ABC_LoginGetCarePackageObjects(AccountNum, NULL, NULL, &pJSON_SNRP2, &pJSON_SNRP3, &pJSON_SNRP4, pError));

            // SNRP's
            ABC_CHECK_RET(ABC_CryptoCreateSNRPForServer(&(pKeys->pSNRP1), pError));
            ABC_CHECK_RET(ABC_CryptoDecodeJSONObjectSNRP(pJSON_SNRP2, &(pKeys->pSNRP2), pError));
            ABC_CHECK_RET(ABC_CryptoDecodeJSONObjectSNRP(pJSON_SNRP3, &(pKeys->pSNRP3), pError));
            ABC_CHECK_RET(ABC_CryptoDecodeJSONObjectSNRP(pJSON_SNRP4, &(pKeys->pSNRP4), pError));

            // L = username
            ABC_BUF_DUP_PTR(pKeys->L, pKeys->szUserName, strlen(pKeys->szUserName));

            // L1 = scrypt(L, SNRP1)
            ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP1, &(pKeys->L1), pError));

            // L4 = scrypt(L, SNRP4)
            ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP4, &(pKeys->L4), pError));

            // add new starting account keys to the key cache
            ABC_CHECK_RET(ABC_LoginAddToKeyCache(pKeys, pError));
            pFinalKeys = pKeys;
            pKeys = NULL; // so we don't free it at the end

        }
        else
        {
            ABC_RET_ERROR(ABC_CC_AccountDoesNotExist, "No account by that name");
        }
    }

    // at this point there is now one in the cache and it is pFinalKeys
    // but it may or may not have password keys

    // Fetch login package objects
    ABC_CHECK_RET(
        ABC_LoginGetLoginPackageObjects(
            pFinalKeys->accountNum, NULL,
            &pJSON_EMK, &pJSON_ESyncKey, NULL, &pJSON_ELRA3, pError));

    // try to decrypt RepoAcctKey
    if (pJSON_ESyncKey)
    {
        tABC_CC CC_Decrypt = ABC_CryptoDecryptJSONObject(pJSON_ESyncKey, pFinalKeys->L4, &REPO_JSON, pError);

        // check the results
        if (ABC_CC_DecryptFailure == CC_Decrypt)
        {
            ABC_RET_ERROR(ABC_CC_BadPassword, "Could not decrypt RepoAcctKey - bad L4");
        }
        else if (ABC_CC_Ok != CC_Decrypt)
        {
            cc = CC_Decrypt;
            goto exit;
        }

        tABC_U08Buf FinalSyncKey;
        ABC_BUF_DUP(FinalSyncKey, REPO_JSON);
        ABC_BUF_APPEND_PTR(FinalSyncKey, "", 1);
        pFinalKeys->szRepoAcctKey = (char*) ABC_BUF_PTR(FinalSyncKey);
    }
    else
    {
        pFinalKeys->szRepoAcctKey = NULL;
    }

    // if we are given a password
    if (NULL != szPassword)
    {
        // if there is no key in the cache, let's add the keys we can with a password
        if (NULL == pFinalKeys->szPassword)
        {
            // P = password
            ABC_BUF_DUP_PTR(P, szPassword, strlen(szPassword));

            // LP = L + P
            ABC_BUF_DUP(LP, pFinalKeys->L);
            ABC_BUF_APPEND(LP, P);

            // LP2 = Scrypt(L + P, SNRP2)
            ABC_CHECK_RET(ABC_CryptoScryptSNRP(LP, pFinalKeys->pSNRP2, &LP2, pError));

            // try to decrypt MK
            tABC_CC CC_Decrypt =
                ABC_CryptoDecryptJSONObject(pJSON_EMK, LP2, &MK, pError);

            // check the results
            if (ABC_CC_DecryptFailure == CC_Decrypt)
            {
                // the assumption here is that this specific error is due to a bad password
                ABC_RET_ERROR(ABC_CC_BadPassword, "Could not decrypt MK - bad password");
            }
            else if (ABC_CC_Ok != CC_Decrypt)
            {
                // this was an error other than just a bad key so we need to treat this like an error
                cc = CC_Decrypt;
                goto exit;
            }

            if (pJSON_ELRA3 != NULL)
            {
                ABC_CHECK_RET(ABC_CryptoDecryptJSONObject(pJSON_ELRA3, LP2, &LRA3, pError));
            }

            // if we got here, then the password was good so we can add what we just calculated to the keys
            ABC_STRDUP(pFinalKeys->szPassword, szPassword);
            ABC_BUF_SET(pFinalKeys->MK, MK);
            ABC_BUF_CLEAR(MK);
            ABC_BUF_SET(pFinalKeys->P, P);
            ABC_BUF_CLEAR(P);
            ABC_BUF_SET(pFinalKeys->LP, LP);
            ABC_BUF_CLEAR(LP);
            ABC_BUF_SET(pFinalKeys->LP2, LP2);
            ABC_BUF_CLEAR(LP2);
            if (pJSON_ELRA3 != NULL)
            {
                ABC_BUF_SET(pFinalKeys->LRA3, LRA3);
                ABC_BUF_CLEAR(LRA3);
            }
        }
        else
        {
            // make sure it is correct
            if (0 != strcmp(pFinalKeys->szPassword, szPassword))
            {
                ABC_RET_ERROR(ABC_CC_BadPassword, "Password is incorrect");
            }
        }
    }

    // if they wanted the keys
    if (ppKeys)
    {
        *ppKeys = pFinalKeys;
    }

exit:
    if (pKeys)
    {
        ABC_LoginFreeAccountKeys(pKeys);
        ABC_CLEAR_FREE(pKeys, sizeof(tAccountKeys));
    }
    if (pJSON_SNRP2)    json_decref(pJSON_SNRP2);
    if (pJSON_SNRP3)    json_decref(pJSON_SNRP3);
    if (pJSON_SNRP4)    json_decref(pJSON_SNRP4);
    if (pJSON_EMK)      json_decref(pJSON_EMK);
    if (pJSON_ESyncKey) json_decref(pJSON_ESyncKey);
    if (pJSON_Root)     json_decref(pJSON_Root);
    ABC_BUF_FREE(REPO_JSON);
    ABC_BUF_FREE(P);
    ABC_BUF_FREE(LP);
    ABC_BUF_FREE(LP2);

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Retrieves the specified key from the key cache
 * if the account associated with the username and password is not currently in the cache, it is added
 */
static
tABC_CC ABC_LoginGetKey(const char *szUserName, const char *szPassword, tABC_LoginKey keyType, tABC_U08Buf *pKey, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;

    tAccountKeys *pKeys = NULL;
    json_t       *pJSON_ERQ     = NULL;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(pKey);

    // make sure the account is in the cache
    ABC_CHECK_RET(ABC_LoginCacheKeys(szUserName, szPassword, &pKeys, pError));
    ABC_CHECK_ASSERT(NULL != pKeys, ABC_CC_Error, "Expected to find account keys in cache.");

    switch(keyType)
    {
        case ABC_LoginKey_L1:
            // L1 = Scrypt(L, SNRP1)
            if (NULL == ABC_BUF_PTR(pKeys->L1))
            {
                ABC_CHECK_ASSERT(NULL != ABC_BUF_PTR(pKeys->L), ABC_CC_Error, "Expected to find L in key cache");
                ABC_CHECK_ASSERT(NULL != pKeys->pSNRP1, ABC_CC_Error, "Expected to find SNRP1 in key cache");
                ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP1, &(pKeys->L1), pError));
            }
            ABC_BUF_SET(*pKey, pKeys->L1);
            break;

        case ABC_LoginKey_L4:
            // L4 = Scrypt(L, SNRP4)
            if (NULL == ABC_BUF_PTR(pKeys->L4))
            {
                ABC_CHECK_ASSERT(NULL != ABC_BUF_PTR(pKeys->L), ABC_CC_Error, "Expected to find L in key cache");
                ABC_CHECK_ASSERT(NULL != pKeys->pSNRP4, ABC_CC_Error, "Expected to find SNRP4 in key cache");
                ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->L, pKeys->pSNRP4, &(pKeys->L4), pError));
            }
            ABC_BUF_SET(*pKey, pKeys->L4);
            break;

        case ABC_LoginKey_LP1:
            if (NULL == ABC_BUF_PTR(pKeys->LP1))
            {
                ABC_BUF_DUP(pKeys->LP, pKeys->L);
                ABC_BUF_APPEND(pKeys->LP, pKeys->P);
                ABC_CHECK_RET(ABC_CryptoScryptSNRP(pKeys->LP, pKeys->pSNRP1, &(pKeys->LP1), pError));
            }
            ABC_CHECK_ASSERT(NULL != ABC_BUF_PTR(pKeys->LP1), ABC_CC_Error, "Expected to find LP1 in key cache");
            ABC_BUF_SET(*pKey, pKeys->LP1);
            break;

        case ABC_LoginKey_MK:
        case ABC_LoginKey_LP2:
            // this should already be in the cache
            ABC_CHECK_ASSERT(NULL != ABC_BUF_PTR(pKeys->MK), ABC_CC_Error, "Expected to find MK in key cache");
            ABC_BUF_SET(*pKey, pKeys->MK);
            break;

        case ABC_LoginKey_RepoAccountKey:
            // this should already be in the cache
            if (NULL == pKeys->szRepoAcctKey)
            {
            }
            ABC_BUF_SET_PTR(*pKey, (unsigned char *)pKeys->szRepoAcctKey, sizeof(pKeys->szRepoAcctKey) + 1);
            break;

        case ABC_LoginKey_RQ:
            // RQ - if ERQ available
            if (NULL == ABC_BUF_PTR(pKeys->RQ))
            {
                // get L4
                tABC_U08Buf L4;
                ABC_CHECK_RET(ABC_LoginGetKey(szUserName, szPassword, ABC_LoginKey_L4, &L4, pError));

                // get ERQ
                int AccountNum = -1;
                ABC_CHECK_RET(ABC_LoginDirGetNumber(szUserName, &AccountNum, pError));
                ABC_CHECK_RET(ABC_LoginGetCarePackageObjects(AccountNum, NULL, &pJSON_ERQ, NULL, NULL, NULL, pError));

                // RQ - if ERQ available
                if (pJSON_ERQ != NULL)
                {
                    ABC_CHECK_RET(ABC_CryptoDecryptJSONObject(pJSON_ERQ, L4, &(pKeys->RQ), pError));
                }
                else
                {
                    ABC_RET_ERROR(ABC_CC_NoRecoveryQuestions, "There are no recovery questions for this user");
                }
            }
            ABC_BUF_SET(*pKey, pKeys->RQ);
            break;

        default:
            ABC_RET_ERROR(ABC_CC_Error, "Unknown key type");
            break;

    };

exit:
    if (pJSON_ERQ)  json_decref(pJSON_ERQ);

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Check that the recovery answers for a given account are valid
 * @param pbValid true is stored in here if they are correct
 */
tABC_CC ABC_LoginCheckRecoveryAnswers(const char *szUserName,
                                      const char *szRecoveryAnswers,
                                      bool *pbValid,
                                      tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    tAccountKeys *pKeys    = NULL;
    tABC_U08Buf LRA        = ABC_BUF_NULL;
    tABC_U08Buf LRA3       = ABC_BUF_NULL;
    tABC_U08Buf LRA1       = ABC_BUF_NULL;
    tABC_U08Buf LP2        = ABC_BUF_NULL;
    json_t *pJSON_ELP2     = NULL;
    char *szAccountDir     = NULL;
    char *szAccountSyncDir = NULL;
    char *szFilename       = NULL;
    bool bExists           = false;

    // Use for the remote answers check
    tABC_U08Buf L           = ABC_BUF_NULL;
    tABC_U08Buf L1          = ABC_BUF_NULL;
    tABC_U08Buf LP1_NULL    = ABC_BUF_NULL;
    char *szLoginPackage    = NULL;
    tABC_CryptoSNRP *pSNRP1 = NULL;

    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_NULL(szRecoveryAnswers);
    ABC_CHECK_NULL(pbValid);
    *pbValid = false;

    // If we have the care package cached, check recovery answers remotely
    // and if successful, setup the account locally
    if (gCarePackageCache)
    {
        ABC_BUF_DUP_PTR(L, szUserName, strlen(szUserName));

        ABC_CHECK_RET(ABC_CryptoCreateSNRPForServer(&pSNRP1, pError));
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(L, pSNRP1, &L1, pError));

        ABC_BUF_DUP(LRA, L);
        ABC_BUF_APPEND_PTR(LRA, szRecoveryAnswers, strlen(szRecoveryAnswers));
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(LRA, pSNRP1, &LRA1, pError));

        // Fetch LoginPackage using LRA1, if successful, szRecoveryAnswers are correct
        ABC_CHECK_RET(ABC_LoginServerGetLoginPackage(L1, LP1_NULL, LRA1, &szLoginPackage, pError));

        // Setup initial account and set the care package
        ABC_CHECK_RET(
            ABC_LoginInitPackages(szUserName, L1,
                                  gCarePackageCache, szLoginPackage,
                                  &szAccountDir, pError));
        ABC_FREE_STR(gCarePackageCache);
        gCarePackageCache = NULL;

        // We have the care package so fetch keys without password
        ABC_CHECK_RET(ABC_LoginCacheKeys(szUserName, NULL, &pKeys, pError));

        // Setup the account repo and sync
        ABC_CHECK_RET(ABC_LoginRepoSetup(pKeys, pError));
    }

    // pull this account into the cache
    ABC_CHECK_RET(ABC_LoginCacheKeys(szUserName, NULL, &pKeys, pError));

    // create our LRA (L + RA) with the answers given
    ABC_BUF_DUP(LRA, pKeys->L);
    ABC_BUF_APPEND_PTR(LRA, szRecoveryAnswers, strlen(szRecoveryAnswers));

    // if the cache has an LRA
    if (ABC_BUF_PTR(pKeys->LRA) != NULL)
    {
        // check if they are equal
        if (ABC_BUF_SIZE(LRA) == ABC_BUF_SIZE(pKeys->LRA))
        {
            if (0 == memcmp(ABC_BUF_PTR(LRA), ABC_BUF_PTR(pKeys->LRA), ABC_BUF_SIZE(LRA)))
            {
                *pbValid = true;
            }
        }
    }
    else
    {
        // create our LRA3 = Scrypt(L + RA, SNRP3)
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(LRA, pKeys->pSNRP3, &LRA3, pError));

        // create our LRA1 = Scrypt(L + RA, SNRP1)
        ABC_CHECK_RET(ABC_CryptoScryptSNRP(LRA, pKeys->pSNRP1, &LRA1, pError));

        ABC_CHECK_RET(ABC_LoginGetSyncDirName(szUserName, &szAccountSyncDir, pError));
        ABC_CHECK_RET(ABC_FileIOFileExists(szAccountSyncDir, &bExists, pError));

        // attempt to decode ELP2
        ABC_CHECK_RET(
            ABC_LoginGetLoginPackageObjects(pKeys->accountNum, NULL, NULL, NULL, &pJSON_ELP2, NULL, pError));
        tABC_CC CC_Decrypt = ABC_CryptoDecryptJSONObject(pJSON_ELP2, LRA3, &LP2, pError);

        // check the results
        if (ABC_CC_Ok == CC_Decrypt)
        {
            *pbValid = true;
        }
        else if (ABC_CC_DecryptFailure != CC_Decrypt)
        {
            // this was an error other than just a bad key so we need to treat this like an error
            cc = CC_Decrypt;
            goto exit;
        }
        else
        {
            // clear the error because we know why it failed and we will set that in the pbValid
            ABC_SET_ERR_CODE(pError, ABC_CC_Ok);
        }

        // if we were successful, save our keys in the cache since we spent the time to create them
        if (*pbValid == true)
        {
            ABC_BUF_SET(pKeys->LRA, LRA);
            ABC_BUF_CLEAR(LRA); // so we don't free as we exit
            ABC_BUF_SET(pKeys->LRA3, LRA3);
            ABC_BUF_CLEAR(LRA3); // so we don't free as we exit
            ABC_BUF_SET(pKeys->LRA1, LRA1);
            ABC_BUF_CLEAR(LRA1); // so we don't free as we exit
            ABC_BUF_SET(pKeys->LP2, LP2);
            ABC_BUF_CLEAR(LP2); // so we don't free as we exit
        }
    }
exit:
    ABC_BUF_FREE(LRA);
    ABC_BUF_FREE(LRA3);
    ABC_BUF_FREE(LRA1);
    ABC_BUF_FREE(LP2);
    ABC_FREE_STR(szAccountDir);
    ABC_FREE_STR(szAccountSyncDir);
    ABC_FREE_STR(szFilename);
    if (pJSON_ELP2) json_decref(pJSON_ELP2);

    ABC_BUF_FREE(L1);
    ABC_FREE_STR(szLoginPackage);
    ABC_CryptoFreeSNRP(&pSNRP1);

    return cc;
}

tABC_CC ABC_LoginFetchRecoveryQuestions(const char *szUserName, char **szRecoveryQuestions, tABC_Error *pError)
{
    ABC_DebugLog("%s called", __FUNCTION__);

    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    char *szCarePackage          = NULL;
    tABC_U08Buf L                = ABC_BUF_NULL;
    tABC_U08Buf L1               = ABC_BUF_NULL;
    tABC_U08Buf L4               = ABC_BUF_NULL;
    tABC_U08Buf RQ               = ABC_BUF_NULL;
    tABC_CryptoSNRP *pSNRP1      = NULL;
    tABC_CryptoSNRP *pSNRP4      = NULL;
    json_t          *pJSON_ERQ   = NULL;
    json_t          *pJSON_SNRP4 = NULL;

    // Create L, SNRP1, L1
    ABC_BUF_DUP_PTR(L, szUserName, strlen(szUserName));
    ABC_CHECK_RET(ABC_CryptoCreateSNRPForServer(&pSNRP1, pError));
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(L, pSNRP1, &L1, pError));

    //  Download CarePackage.json
    ABC_CHECK_RET(ABC_LoginServerGetCarePackage(L1, &szCarePackage, pError));

    // Set the CarePackage cache
    ABC_STRDUP(gCarePackageCache, szCarePackage);

    // get ERQ and SNRP4
    ABC_CHECK_RET(ABC_LoginGetCarePackageObjects(0, gCarePackageCache, &pJSON_ERQ, NULL, NULL, &pJSON_SNRP4, pError));

    // Create L4
    ABC_CHECK_RET(ABC_CryptoDecodeJSONObjectSNRP(pJSON_SNRP4, &pSNRP4, pError));
    ABC_CHECK_RET(ABC_CryptoScryptSNRP(L, pSNRP4, &L4, pError));

    tABC_U08Buf FinalRQ = ABC_BUF_NULL;
    // RQ - if ERQ available
    if (pJSON_ERQ != NULL)
    {
        ABC_CHECK_RET(ABC_CryptoDecryptJSONObject(pJSON_ERQ, L4, &RQ, pError));
        ABC_BUF_DUP(FinalRQ, RQ);
        ABC_BUF_APPEND_PTR(FinalRQ, "", 1);
    }
    else
    {
        ABC_BUF_DUP_PTR(FinalRQ, "", 1);
    }
    *szRecoveryQuestions = (char *) ABC_BUF_PTR(FinalRQ);
exit:
    ABC_FREE_STR(szCarePackage);
    ABC_BUF_FREE(RQ);
    ABC_BUF_FREE(L);
    ABC_BUF_FREE(L1);
    ABC_BUF_FREE(L4);
    if (pJSON_ERQ) json_decref(pJSON_ERQ);
    if (pJSON_SNRP4) json_decref(pJSON_SNRP4);
    return cc;
}

/**
 * Get the recovery questions for a given account.
 *
 * The questions will be returned in a single allocated string with
 * each questions seperated by a newline.
 *
 * @param szUserName                UserName for the account
 * @param pszQuestions              Pointer into which allocated string should be stored.
 * @param pError                    A pointer to the location to store the error if there is one
 */
tABC_CC ABC_LoginGetRecoveryQuestions(const char *szUserName,
                                      char **pszQuestions,
                                      tABC_Error *pError)
{
    ABC_DebugLog("%s called", __FUNCTION__);

    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_ASSERT(strlen(szUserName) > 0, ABC_CC_Error, "No username provided");
    ABC_CHECK_NULL(pszQuestions);
    *pszQuestions = NULL;

    // Free up care package cache if set
    ABC_FREE_STR(gCarePackageCache);
    gCarePackageCache = NULL;

    // check that this is a valid user
    if (ABC_LoginDirExists(szUserName, NULL) != ABC_CC_Ok)
    {
        // Try the server
        ABC_CHECK_RET(ABC_LoginFetchRecoveryQuestions(szUserName, pszQuestions, pError));
    }
    else
    {
        // Get RQ for this user
        tABC_U08Buf RQ;
        ABC_CHECK_RET(ABC_LoginGetKey(szUserName, NULL, ABC_LoginKey_RQ, &RQ, pError));
        tABC_U08Buf FinalRQ;
        ABC_BUF_DUP(FinalRQ, RQ);
        ABC_BUF_APPEND_PTR(FinalRQ, "", 1);
        *pszQuestions = (char *)ABC_BUF_PTR(FinalRQ);
    }

exit:

    return cc;
}

/**
 * Obtains the information needed to access the sync dir for a given account.
 *
 * @param szUserName UserName for the account to access
 * @param szPassword Password for the account to access
 * @param ppKeys     Location to store returned pointer. The caller must free the structure.
 * @param pError     A pointer to the location to store the error if there is one
 */
tABC_CC ABC_LoginGetSyncKeys(const char *szUserName,
                             const char *szPassword,
                             tABC_SyncKeys **ppKeys,
                             tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    tABC_SyncKeys *pKeys = NULL;
    tABC_U08Buf tempSyncKey;

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_ASSERT(strlen(szUserName) > 0, ABC_CC_Error, "No username provided");
    ABC_CHECK_NULL(szPassword);
    ABC_CHECK_ASSERT(strlen(szPassword) > 0, ABC_CC_Error, "No password provided");
    ABC_CHECK_NULL(ppKeys);

    ABC_ALLOC(pKeys, sizeof(tABC_SyncKeys));
    ABC_CHECK_RET(ABC_LoginGetSyncDirName(szUserName, &pKeys->szSyncDir, pError));
    ABC_CHECK_RET(ABC_LoginGetKey(szUserName, szPassword, ABC_LoginKey_RepoAccountKey, &tempSyncKey, pError));
    ABC_CHECK_RET(ABC_LoginGetKey(szUserName, szPassword, ABC_LoginKey_MK, &pKeys->MK, pError));
    pKeys->szSyncKey = (char*)tempSyncKey.p;

    *ppKeys = pKeys;
    pKeys = NULL;

exit:
    if (pKeys)          ABC_SyncFreeKeys(pKeys);

    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Obtains the information needed to access the server for a given account.
 *
 * @param szUserName UserName for the account to access
 * @param szPassword Password for the account to access
 * @param pL1        A buffer to receive L1. Do *not* free this.
 * @param pLP1       A buffer to receive LP1. Do *not* free this.
 * @param pError     A pointer to the location to store the error if there is one
 */

tABC_CC ABC_LoginGetServerKeys(const char *szUserName,
                               const char *szPassword,
                               tABC_U08Buf *pL1,
                               tABC_U08Buf *pLP1,
                               tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    ABC_CHECK_RET(ABC_LoginMutexLock(pError));
    ABC_CHECK_NULL(szUserName);
    ABC_CHECK_ASSERT(strlen(szUserName) > 0, ABC_CC_Error, "No username provided");
    ABC_CHECK_NULL(szPassword);
    ABC_CHECK_ASSERT(strlen(szPassword) > 0, ABC_CC_Error, "No password provided");
    ABC_CHECK_NULL(pL1);
    ABC_CHECK_NULL(pLP1);

    ABC_CHECK_RET(ABC_LoginGetKey(szUserName, szPassword, ABC_LoginKey_L1, pL1, pError));
    ABC_CHECK_RET(ABC_LoginGetKey(szUserName, szPassword, ABC_LoginKey_LP1, pLP1, pError));

exit:
    ABC_LoginMutexUnlock(NULL);
    return cc;
}

/**
 * Sync the account data
 *
 * @param szUserName    UserName for the account associated with the settings
 * @param szPassword    Password for the account associated with the settings
 * @param pError        A pointer to the location to store the error if there is one
 */
tABC_CC ABC_LoginSyncData(const char *szUserName,
                          const char *szPassword,
                          int *pDirty,
                          tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    tABC_SyncKeys *pKeys = NULL;
    ABC_CHECK_RET(ABC_LoginGetSyncKeys(szUserName, szPassword, &pKeys, pError));
    ABC_CHECK_RET(ABC_SyncRepo(pKeys->szSyncDir, pKeys->szSyncKey, pDirty, pError));

exit:
    if (pKeys)          ABC_SyncFreeKeys(pKeys);
    return cc;
}

/**
 * Locks the mutex
 *
 * ABC_Wallet uses the same mutex as ABC_Login so that there will be no situation in
 * which one thread is in ABC_Wallet locked on a mutex and calling a thread safe ABC_Login call
 * that is locked from another thread calling a thread safe ABC_Wallet call.
 * In other words, since they call each other, they need to share a recursive mutex.
 */
static
tABC_CC ABC_LoginMutexLock(tABC_Error *pError)
{
    return ABC_MutexLock(pError);
}

/**
 * Unlocks the mutex
 *
 */
static
tABC_CC ABC_LoginMutexUnlock(tABC_Error *pError)
{
    return ABC_MutexUnlock(pError);
}

