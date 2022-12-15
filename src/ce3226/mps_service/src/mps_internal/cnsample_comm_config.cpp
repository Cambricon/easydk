/*************************************************************************
 * Copyright (C) [2021] by Cambricon, Inc.
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *************************************************************************/

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#include <signal.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sched.h>
#include <sys/times.h>
#include <time.h>
#include "signal.h"
#include "cnsample_comm.h"

#define CNCFG_MAX_LINE_LEN                  1024    
#define CNCFG_MAX_VALUE_LEN                 255    
#define CNCFG_MAX_SECTIONNAME_LEN           50      
#define CNCFG_MAX_KEYNAME_LEN               50      
#define CNCFG_STR_RETURN                    "\n"

#define CNCFG_PROFILE_COMMENT_S                         "/*"
#define CNCFG_PROFILE_COMMENT_E                         "*/"
#define CNCFG_PROFILE_COMMENT1                          ";"
#define CNCFG_PROFILE_COMMENT2                          "//"
#define CNCFG_PROFILE_COMMENT3                          "--"
#define CNCFG_PROFILE_COMMENT4                          "#"

#define CNSAMPLE_CONFIG_FILE                            "../cnsampleConfig.ini"
#define CNCFG_SCONFIG_SECTION_SENSORCONFIG              "SensorConfig"
#define CNCFG_SCONFIG_SECTION_SENSORCONFIG_NUM          "SensorNum"
#define CNCFG_SCONFIG_SECTION_SENSORCONFIG_TYPE         "Type"
#define CNCFG_SCONFIG_SECTION_SENSORCONFIG_MIPIDEV      "MipiDev"
#define CNCFG_SCONFIG_SECTION_SENSORCONFIG_BUSID        "BusID"
#define CNCFG_SCONFIG_SECTION_SENSORCONFIG_SNSCLKID     "SnsClkId"

static cnS32_t cnsampleCommIniStringUpper(cnChar_t *ps8Src)
{
    cnU16_t u16Loop = 0;
    cnChar_t s8Difference = 'A' - 'a';

    if (NULL == ps8Src)
    {
        CNSAMPLE_TRACE("NULL PTR!\n");
        return CN_FAILURE; 
    }

    for (u16Loop = 0; u16Loop < strlen(ps8Src); u16Loop++)
    {
        if ('a' <= ps8Src[u16Loop] && 'z' >= ps8Src[u16Loop])
        {
            ps8Src[u16Loop] += s8Difference;
        }
    }

    return CN_SUCCESS;
}

static cnS32_t cnsampleCommIniStringTrim(cnChar_t *ps8Src)
{
    cnS32_t s32RightIndex = 0;
    cnS32_t s32LeftIndex = 0;

    if (NULL == ps8Src)
    {
        CNSAMPLE_TRACE("NULL PTR!\n");
        return CN_FAILURE;
    }

    while ((' ' == ps8Src[s32LeftIndex] || 0x09 == ps8Src[s32LeftIndex]) && '\0' != ps8Src[s32LeftIndex])
    {
        s32LeftIndex++;
    }
    
    if (1 <= strlen(ps8Src))
        s32RightIndex = strlen(ps8Src) - 1;
    else
        return CN_FAILURE;
    while ((' ' == ps8Src[s32RightIndex] || 0x09 == ps8Src[s32RightIndex]) && 0 < s32RightIndex)
    {
        s32RightIndex--;
    }

    if (s32RightIndex >= s32LeftIndex)
    {
        memmove(ps8Src, ps8Src + s32LeftIndex, s32RightIndex - s32LeftIndex + 1);
        ps8Src[s32RightIndex - s32LeftIndex + 1] = '\0';
    }
    else
    {
        ps8Src[0] = '\0';
    }

    return CN_SUCCESS;
}


static cnS32_t cnsampleCommIniMoveToSection(FILE *phFileFd, const cnChar_t *ps8SectionName, cnBool_t bCreate)
{
    cnChar_t as8Row[CNCFG_MAX_LINE_LEN + 1] = {0};
    cnChar_t as8SectionCopy[CNCFG_MAX_SECTIONNAME_LEN + 1] = {0};
    cnU32_t u32Len = 0;
    cnChar_t *ps8IndexTmp = NULL;

    if (NULL == phFileFd || NULL == ps8SectionName || CNCFG_MAX_SECTIONNAME_LEN < strlen(ps8SectionName))
    {
        CNSAMPLE_TRACE("NULL PTR!\n");
        return CN_FAILURE;
    }

    if (0 != fseek(phFileFd, 0, SEEK_SET))
    {
        CNSAMPLE_TRACE("move begin failed,errno=%s!\n",  strerror(errno));
        return CN_FAILURE;
    }

    snprintf(as8SectionCopy, sizeof(as8SectionCopy), "%s", ps8SectionName);
    cnsampleCommIniStringTrim(as8SectionCopy);
    cnsampleCommIniStringUpper(as8SectionCopy);

    do
    {
        if (NULL == fgets(as8Row, CNCFG_MAX_LINE_LEN, phFileFd))
        {
            if (bCreate && feof(phFileFd))
            {
                fputs(CNCFG_STR_RETURN, phFileFd);
                fputs("[", phFileFd);
                fputs(ps8SectionName, phFileFd);
                fputs("]", phFileFd);
                fputs(CNCFG_STR_RETURN, phFileFd);
                fseek(phFileFd, 0, SEEK_END);
                return (ftell(phFileFd));
            }
            else
            {
                CNSAMPLE_TRACE("find section failed\n");
                return CN_FAILURE;
            }
        }

        if ((NULL != (ps8IndexTmp = strstr(as8Row, "\r\n"))) || (NULL != (ps8IndexTmp = strstr(as8Row, "\n"))))
        {
            ps8IndexTmp[0] = '\0';
        }
        cnsampleCommIniStringTrim(as8Row);
        
        u32Len = strlen(as8Row);
        if (2 >= u32Len || '[' != as8Row[0] || ']' != as8Row[u32Len - 1])
        {
            continue;
        }
        
        memmove(as8Row, as8Row + 1, u32Len - 2);
        as8Row[u32Len - 2] = '\0';
        cnsampleCommIniStringTrim(as8Row);
        cnsampleCommIniStringUpper(as8Row);
        /* identical */
        if (0 == strcmp(as8Row, as8SectionCopy))
        {
            return (ftell(phFileFd));
        }
    }while(CN_TRUE);

    CNSAMPLE_TRACE("find section failed\n");
    return CN_FAILURE;
}


static cnS32_t cnsampleCommIniRWKeyValueString(FILE *phFileFd, const cnChar_t *ps8KeyName, cnChar_t *ps8KeyValue, cnS32_t s32OffsetCurSection, cnU32_t u32BufSize, cnBool_t bWrite)
{
    cnChar_t as8Row[CNCFG_MAX_LINE_LEN + 1] = {0};
    cnChar_t as8RowKeyName[CNCFG_MAX_KEYNAME_LEN + 1] = {0};
    cnChar_t as8KeyNameCopy[CNCFG_MAX_KEYNAME_LEN + 1] = {0};
    cnChar_t *ps8EqualPos = 0;
    cnChar_t *ps8Return = 0;
    cnU32_t u32Len = 0;
    static cnBool_t bIsComment = CN_FALSE;
	
    if (NULL == phFileFd || NULL == ps8KeyName || CNCFG_MAX_KEYNAME_LEN < strlen(ps8KeyName) || NULL == ps8KeyValue || CNCFG_MAX_VALUE_LEN < strlen(ps8KeyValue))
    {
        CNSAMPLE_TRACE("NULL PTR!\n");
        return CN_FAILURE;
    }

    snprintf(as8KeyNameCopy, sizeof(as8KeyNameCopy), "%s", ps8KeyName);
    cnsampleCommIniStringTrim(as8KeyNameCopy);
    cnsampleCommIniStringUpper(as8KeyNameCopy);

    fseek(phFileFd, s32OffsetCurSection, SEEK_SET);
    do
    {
        if (NULL == fgets(as8Row, CNCFG_MAX_LINE_LEN, phFileFd))
        {    
            CNSAMPLE_TRACE("ind key failed\n");
            return CN_FAILURE;
        }

        if ((NULL != (ps8Return = strstr(as8Row, "\r\n"))) || (NULL != (ps8Return = strstr(as8Row, "\n"))))
        {
            ps8Return[0] = '\0';
        }
        cnsampleCommIniStringTrim(as8Row);
        if (0 == (u32Len = strlen(as8Row)))
        {
            continue;
        }

        if ('[' == as8Row[0])
        {
            CNSAMPLE_TRACE("find key failed\n");
            return CN_FAILURE;
        }
        
	    if (!bIsComment)
        {
            if (as8Row == strstr(as8Row, CNCFG_PROFILE_COMMENT_S))
            {
                if (NULL == (strstr( as8Row, CNCFG_PROFILE_COMMENT_E)))
                {
                    bIsComment = CN_TRUE;
                }
                continue;
            }
        }
        else
        {
            if (NULL != (strstr(as8Row, CNCFG_PROFILE_COMMENT_E)))
            {
                bIsComment = CN_FALSE;
            }
            continue;
        }

        if (as8Row == strstr(as8Row, CNCFG_PROFILE_COMMENT1) || as8Row == strstr(as8Row, CNCFG_PROFILE_COMMENT2) || as8Row == strstr(as8Row, CNCFG_PROFILE_COMMENT3)
            || as8Row == strstr(as8Row, CNCFG_PROFILE_COMMENT4) || NULL == (ps8EqualPos = strchr(as8Row, '=')))
        {
            continue;
        }

		if (CNCFG_MAX_KEYNAME_LEN < (ps8EqualPos - as8Row))
		{
            CNSAMPLE_TRACE("keynama buffer overflow,row=%s\n",  as8Row);
		    return CN_FAILURE;
		}
	
        memcpy(as8RowKeyName, as8Row, ps8EqualPos - as8Row);
        as8RowKeyName[ps8EqualPos - as8Row] = '\0';
        cnsampleCommIniStringTrim(as8RowKeyName);
        cnsampleCommIniStringUpper(as8RowKeyName);
        if (0 != strcmp(as8RowKeyName, as8KeyNameCopy))  
        {
            continue;
        }

        cnsampleCommIniStringTrim(ps8EqualPos + 1);
        snprintf(ps8KeyValue, u32BufSize, "%s", ps8EqualPos + 1);
        return ftell(phFileFd);
    }while(CN_TRUE);

    CNSAMPLE_TRACE("find key failed\n");
    return CN_FAILURE;
}

static cnS32_t cnsampleCommGetIniKeyStringExt(FILE *phFileFd, const cnChar_t *ps8SectionName, const cnChar_t *ps8KeyName, const cnChar_t *ps8DefaultVal, cnChar_t *ps8ReturnValue, cnU32_t u32BufSize)
{
    cnS32_t s32Offset = 0;

    if (NULL == phFileFd || NULL == ps8SectionName || NULL == ps8KeyName || NULL == ps8ReturnValue)
    {
        CNSAMPLE_TRACE("NULL PTR!\n");
        return CN_FAILURE;
    }
  
    if (NULL != ps8DefaultVal)
    {
        snprintf(ps8ReturnValue, u32BufSize, "%s", ps8DefaultVal);
    }

    if (CN_FAILURE == (s32Offset =  cnsampleCommIniMoveToSection(phFileFd, ps8SectionName, CN_FALSE)))
    {
        CNSAMPLE_TRACE("search section [%s] failed,errno=%s!\n", ps8SectionName, strerror(errno));
        return CN_FAILURE;
    }

    if (CN_FAILURE == cnsampleCommIniRWKeyValueString(phFileFd, ps8KeyName, ps8ReturnValue, s32Offset, u32BufSize, CN_FALSE))
    {
        CNSAMPLE_TRACE("search key [%s] failed,errno=%s!\n", ps8KeyName, strerror(errno));
        return CN_FAILURE;
    }
    
    return CN_SUCCESS;
}

static cnS32_t cnsampleCommGetIniKeyString(const cnChar_t *ps8ProfileName, const cnChar_t *ps8SectionName, const cnChar_t *ps8KeyName, const cnChar_t *ps8DefaultVal, cnChar_t *ps8ReturnValue, cnU32_t u32BufSize)
{
    cnS32_t sRet = CN_TRUE;
    FILE *pfHandle = NULL;

    if (NULL == ps8ProfileName || NULL == ps8SectionName || NULL == ps8KeyName || NULL == ps8ReturnValue)
    {
        CNSAMPLE_TRACE("NULL PTR!\n");
        return CN_FAILURE;
    }

    if (NULL == (pfHandle = fopen(ps8ProfileName, "rb")))
    {
        CNSAMPLE_TRACE("open %s failed,errno=%s!\n", ps8ProfileName, strerror(errno));
        return CN_FAILURE;
    }

    sRet = cnsampleCommGetIniKeyStringExt(pfHandle, ps8SectionName, ps8KeyName, ps8DefaultVal, ps8ReturnValue, u32BufSize);
    fclose(pfHandle);

    return sRet;
}

static cnS32_t cnsampleCommGetIniKeyInt(const cnChar_t *ps8ProfileName, const cnChar_t *ps8SectionName, const cnChar_t *ps8KeyName, const cnS32_t s32DefaultVal, cnS32_t *ps32ReturnValue)
{
    cnS32_t sRet = CN_FAILURE;
    cnChar_t as8KeyValue[CNCFG_MAX_VALUE_LEN + 1] = {0};

    if (NULL == ps8ProfileName || NULL == ps8SectionName || NULL == ps8KeyName || NULL == ps32ReturnValue)
    {
        CNSAMPLE_TRACE("NULL PTR!\n");
        return CN_FAILURE;
    }
  
    *ps32ReturnValue = s32DefaultVal;
    if (CN_FAILURE != (sRet = cnsampleCommGetIniKeyString(ps8ProfileName, ps8SectionName, ps8KeyName, NULL, as8KeyValue, sizeof(as8KeyValue))))
    {
        *ps32ReturnValue = atoi(as8KeyValue);
    }
    
    return sRet;
}

static cnS32_t cnsampleCommCheckSensorCfg(cnS32_t s32SensorId, cnsampleSensorInfo_t* pstSnsConfig)
{
    if (pstSnsConfig->MipiDev > 5)
    {
        CNSAMPLE_TRACE("sensor id %d MipiDev %d not support\n", s32SensorId, pstSnsConfig->MipiDev);
        return CN_FAILURE;
    }  
    
    return CN_SUCCESS;
}

static cnS32_t cnsampleCommGetSingleSensorCfg(cnS32_t s32SensorId, cnsampleSensorInfo_t* pstSnsConfig)
{
    cnChar_t as8KeyValue[CNCFG_MAX_VALUE_LEN + 1] = {0};
    
    snprintf(as8KeyValue, CNCFG_MAX_VALUE_LEN, "sensor.%d", s32SensorId);
    pstSnsConfig->s32SnsId = s32SensorId;
    if (CN_SUCCESS != cnsampleCommGetIniKeyInt(CNSAMPLE_CONFIG_FILE, as8KeyValue, CNCFG_SCONFIG_SECTION_SENSORCONFIG_TYPE, 0, (cnS32_t *)&pstSnsConfig->enSnsType))
    {
        CNSAMPLE_TRACE("get [%s]%s Type failed\n",  CNCFG_SCONFIG_SECTION_SENSORCONFIG, CNCFG_SCONFIG_SECTION_SENSORCONFIG_TYPE);
        return CN_FAILURE;
    }

    if (CN_SUCCESS != cnsampleCommGetIniKeyInt(CNSAMPLE_CONFIG_FILE, as8KeyValue, CNCFG_SCONFIG_SECTION_SENSORCONFIG_MIPIDEV, 0, (cnS32_t *)&pstSnsConfig->MipiDev))
    {
        CNSAMPLE_TRACE("get [%s]%s Type failed\n",  CNCFG_SCONFIG_SECTION_SENSORCONFIG, CNCFG_SCONFIG_SECTION_SENSORCONFIG_MIPIDEV);
        return CN_FAILURE;
    }

    if (CN_SUCCESS != cnsampleCommGetIniKeyInt(CNSAMPLE_CONFIG_FILE, as8KeyValue, CNCFG_SCONFIG_SECTION_SENSORCONFIG_BUSID, 0, (cnS32_t *)&pstSnsConfig->s32BusId))
    {
        CNSAMPLE_TRACE("get [%s]%s Type failed\n",  CNCFG_SCONFIG_SECTION_SENSORCONFIG, CNCFG_SCONFIG_SECTION_SENSORCONFIG_BUSID);
        return CN_FAILURE;
    }

    if (CN_SUCCESS != cnsampleCommGetIniKeyInt(CNSAMPLE_CONFIG_FILE, as8KeyValue, CNCFG_SCONFIG_SECTION_SENSORCONFIG_SNSCLKID, 0, (cnS32_t *)&pstSnsConfig->s32SnsClkId))
    {
        CNSAMPLE_TRACE("get [%s]%s Type failed\n",  CNCFG_SCONFIG_SECTION_SENSORCONFIG, CNCFG_SCONFIG_SECTION_SENSORCONFIG_SNSCLKID);
        return CN_FAILURE;
    }

    return CN_SUCCESS;
}

cnS32_t cnsampleCommGetSensorCfg(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t i = 0;
    
    if (NULL == pstViConfig)
    {
        CNSAMPLE_TRACE("NULL PTR!\n");
        return CN_FAILURE;
    }

    if (0 != access(CNSAMPLE_CONFIG_FILE, F_OK))
    {
        CNSAMPLE_TRACE("file=%s is no exist,error=%s\n", CNSAMPLE_CONFIG_FILE, strerror(errno));
        return CN_FAILURE;
    }

    /* sensor num */
    if (CN_SUCCESS != cnsampleCommGetIniKeyInt(CNSAMPLE_CONFIG_FILE, CNCFG_SCONFIG_SECTION_SENSORCONFIG, CNCFG_SCONFIG_SECTION_SENSORCONFIG_NUM, 0, &pstViConfig->s32WorkingViNum))
    {
        CNSAMPLE_TRACE("get [%s]%s failed\n",  CNCFG_SCONFIG_SECTION_SENSORCONFIG, CNCFG_SCONFIG_SECTION_SENSORCONFIG_NUM);
        return CN_FAILURE;
    }

    if (pstViConfig->s32WorkingViNum <= 0 || pstViConfig->s32WorkingViNum > VI_MAX_DEV_NUM)
    {
        CNSAMPLE_TRACE("senosrnum %d error\n", pstViConfig->s32WorkingViNum);
        return CN_FAILURE;
    }
    
    CNSAMPLE_TRACE("sensor num = %d\n", pstViConfig->s32WorkingViNum);
    for (i = 0; i < pstViConfig->s32WorkingViNum; i++)
    {
        if (CN_SUCCESS != cnsampleCommGetSingleSensorCfg(i, &pstViConfig->astViInfo[i].stSnsInfo))
        {
            CNSAMPLE_TRACE("cnsampleCommGetSingleSensorCfg sensor id %d error\n", i);
            return CN_FAILURE;
        }

        if (CN_SUCCESS != cnsampleCommCheckSensorCfg(i, &pstViConfig->astViInfo[i].stSnsInfo))
        {
            CNSAMPLE_TRACE("cnsampleCommCheckSensorCfg sensor id %d error\n", i);
            return CN_FAILURE;
        }
        
        CNSAMPLE_TRACE("sensor id %d type %d mipidev %d busid %d snsclkid %d\n", i,\
            pstViConfig->astViInfo[i].stSnsInfo.enSnsType, pstViConfig->astViInfo[i].stSnsInfo.MipiDev, pstViConfig->astViInfo[i].stSnsInfo.s32BusId, pstViConfig->astViInfo[i].stSnsInfo.s32SnsClkId);
    }
    return CN_SUCCESS;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */



