/**
 * @file
 * AirBitz URL functions.
 *
 * This file contains all of the functions associated with sending and receiving
 * data to and from servers.
 *
 * @author Adam Harris
 * @version 1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include "ABC.h"
#include "ABC_FileIO.h"
#include "ABC_Util.h"
#include "ABC_Debug.h"
#include "ABC_URL.h"

static bool gbInitialized = false;

static size_t ABC_URLCurlWriteData(void *pBuffer, size_t memberSize, size_t numMembers, void *pUserData);

/**
 * Initialize the URL system
 */
tABC_CC ABC_URLInitialize(tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    ABC_CHECK_ASSERT(false == gbInitialized, ABC_CC_Reinitialization, "The URL has already been initalized");

    // initialize curl
    CURLcode curlCode;
    if ((curlCode = curl_global_init(CURL_GLOBAL_ALL)) != 0)
    {
        ABC_DebugLog("Curl init failed: %d\n", curlCode);
        ABC_RET_ERROR(ABC_CC_URLError, "Curl init failed");
    }

    gbInitialized = true;

exit:

    return cc;
}

/**
 * Shut down the URL system
 */
void ABC_URLTerminate()
{
    if (gbInitialized == true)
    {
        // cleanup curl
        curl_global_cleanup();

        gbInitialized = false;
    }
}

/**
 * Makes a URL request. 
 * @param szURL         The request URL.
 * @param pData         The location to store the results. The caller is responsible for free'ing this.
 */
tABC_CC ABC_URLRequest(const char *szURL, tABC_U08Buf *pData, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    tABC_U08Buf Data = ABC_BUF_NULL;
    CURL *pCurlHandle = NULL;

    ABC_CHECK_ASSERT(true == gbInitialized, ABC_CC_NotInitialized, "The URL system has not been initalized");
    ABC_CHECK_NULL(szURL);
    ABC_CHECK_NULL(pData);

    // start with no data
    ABC_BUF_CLEAR(*pData);

    CURLcode curlCode = 0;
    pCurlHandle = curl_easy_init();

    if ((curlCode = curl_easy_setopt(pCurlHandle, CURLOPT_URL, szURL)) != 0)
    {
        ABC_DebugLog("Curl easy setopt failed: %d\n", curlCode);
        ABC_RET_ERROR(ABC_CC_URLError, "Curl easy setopt failed");
    }

    if ((curlCode = curl_easy_setopt(pCurlHandle, CURLOPT_WRITEFUNCTION, ABC_URLCurlWriteData)) != 0)
    {
        ABC_DebugLog("Curl easy setopt failed: %d\n", curlCode);
        ABC_RET_ERROR(ABC_CC_URLError, "Curl easy setopt failed");
    }

    if ((curlCode = curl_easy_setopt(pCurlHandle, CURLOPT_WRITEDATA, &Data)) != 0)
    {
        ABC_DebugLog("Curl easy setopt failed: %d\n", curlCode);
        ABC_RET_ERROR(ABC_CC_URLError, "Curl easy setopt failed");
    }

    if ((curlCode = curl_easy_perform(pCurlHandle)) != 0)
    {
        ABC_DebugLog("Curl easy perform failed: %d\n", curlCode);
        ABC_RET_ERROR(ABC_CC_URLError, "Curl easy perform failed");
    }

    // store the data in the user's buffer
    ABC_BUF_SET(*pData, Data);
    ABC_BUF_CLEAR(Data);

exit:
    ABC_BUF_FREE(Data);
    curl_easy_cleanup(pCurlHandle);
    
    return cc;
}

/**
 * Makes a URL request and returns results in a string.
 * @param szURL         The request URL.
 * @param pszResults    The location to store the allocated string with results. 
 *                      The caller is responsible for free'ing this.
 */
tABC_CC ABC_URLRequestString(const char *szURL,
                             char **pszResults,
                             tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    tABC_U08Buf Data;

    ABC_CHECK_ASSERT(true == gbInitialized, ABC_CC_NotInitialized, "The URL system has not been initalized");
    ABC_CHECK_NULL(szURL);
    ABC_CHECK_NULL(pszResults);

    // make the request
    ABC_CHECK_RET(ABC_URLRequest(szURL, &Data, pError));

    // add the null
    ABC_BUF_APPEND_PTR(Data, "", 1);

    // assign the results
    *pszResults = (char *)ABC_BUF_PTR(Data);
    ABC_BUF_CLEAR(Data);

exit:
    ABC_BUF_FREE(Data);

    return cc;
}

/**
 * Makes a URL post request.
 * @param szURL         The request URL.
 * @param szPostData    The data to be posted in the request
 * @param pData         The location to store the results. The caller is responsible for free'ing this.
 */
tABC_CC ABC_URLPost(const char *szURL, const char *szPostData, tABC_U08Buf *pData, tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    tABC_U08Buf Data = ABC_BUF_NULL;
    CURL *pCurlHandle = NULL;

    ABC_CHECK_ASSERT(true == gbInitialized, ABC_CC_NotInitialized, "The URL system has not been initalized");
    ABC_CHECK_NULL(szURL);
    ABC_CHECK_NULL(szPostData);
    ABC_CHECK_NULL(pData);

    // start with no data
    ABC_BUF_CLEAR(*pData);

    CURLcode curlCode = 0;
    pCurlHandle = curl_easy_init();
    
    if ((curlCode = curl_easy_setopt(pCurlHandle, CURLOPT_URL, szURL)) != 0)
    {
        ABC_DebugLog("Curl easy setopt failed: %d\n", curlCode);
        ABC_RET_ERROR(ABC_CC_URLError, "Curl easy setopt failed");
    }

    if ((curlCode = curl_easy_setopt(pCurlHandle, CURLOPT_WRITEFUNCTION, ABC_URLCurlWriteData)) != 0)
    {
        ABC_DebugLog("Curl easy setopt failed: %d\n", curlCode);
        ABC_RET_ERROR(ABC_CC_URLError, "Curl easy setopt failed");
    }

    if ((curlCode = curl_easy_setopt(pCurlHandle, CURLOPT_WRITEDATA, &Data)) != 0)
    {
        ABC_DebugLog("Curl easy setopt failed: %d\n", curlCode);
        ABC_RET_ERROR(ABC_CC_URLError, "Curl easy setopt failed");
    }

    if ((curlCode = curl_easy_setopt(pCurlHandle, CURLOPT_POSTFIELDS, szPostData)) != 0)
    {
        ABC_DebugLog("Curl easy setopt failed: %d\n", curlCode);
        ABC_RET_ERROR(ABC_CC_URLError, "Curl easy setopt failed");
    }

    if ((curlCode = curl_easy_perform(pCurlHandle)) != 0)
    {
        ABC_DebugLog("Curl easy perform failed: %d\n", curlCode);
        ABC_RET_ERROR(ABC_CC_URLError, "Curl easy perform failed");
    }

    // store the data in the user's buffer
    ABC_BUF_SET(*pData, Data);
    ABC_BUF_CLEAR(Data);

exit:
    ABC_BUF_FREE(Data);
    curl_easy_cleanup(pCurlHandle);
    
    return cc;
}

/**
 * Makes a URL post request and returns results in a string.
 * @param szURL         The request URL.
 * @param szPostData    The data to be posted in the request
 * @param pszResults    The location to store the allocated string with results.
 *                      The caller is responsible for free'ing this.
 */
tABC_CC ABC_URLPostString(const char *szURL,
                          const char *szPostData,
                          char **pszResults,
                          tABC_Error *pError)
{
    tABC_CC cc = ABC_CC_Ok;
    ABC_SET_ERR_CODE(pError, ABC_CC_Ok);

    tABC_U08Buf Data;

    ABC_CHECK_ASSERT(true == gbInitialized, ABC_CC_NotInitialized, "The URL system has not been initalized");
    ABC_CHECK_NULL(szURL);
    ABC_CHECK_NULL(szPostData);
    ABC_CHECK_NULL(pszResults);

    // make the request
    ABC_CHECK_RET(ABC_URLPost(szURL, szPostData, &Data, pError));

    // add the null
    ABC_BUF_APPEND_PTR(Data, "", 1);

    // assign the results
    *pszResults = (char *)ABC_BUF_PTR(Data);
    ABC_BUF_CLEAR(Data);

exit:
    ABC_BUF_FREE(Data);
    
    return cc;
}


/**
 * This is the function that gets called by CURL when it has data to be saved from a request.
 * @param pBuffer Pointer to incoming data
 * @param memberSize Size of the members
 * @param numMembers Number of members in the buffer
 * @param pUserData  User data specified initial calls
 */
static
size_t ABC_URLCurlWriteData(void *pBuffer, size_t memberSize, size_t numMembers, void *pUserData)
{
    tABC_U08Buf *pCurlBuffer = (tABC_U08Buf *)pUserData;
    unsigned int dataAvailLength = (unsigned int) numMembers * (unsigned int) memberSize;
    size_t amountWritten = 0;

    if (pCurlBuffer)
    {
        // if we don't have any buffer allocated yet
        if (ABC_BUF_PTR(*pCurlBuffer) == NULL)
        {
            ABC_BUF_DUP_PTR(*pCurlBuffer, pBuffer, dataAvailLength);
        }
        else
        {
            ABC_BUF_APPEND_PTR(*pCurlBuffer, pBuffer, dataAvailLength);
        }
        amountWritten = dataAvailLength;
    }

    return amountWritten;
}
