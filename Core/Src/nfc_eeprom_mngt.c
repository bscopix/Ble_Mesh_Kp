/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : nfc_eeprom_mngt.c
  * @brief          : communication and storing data to ctor10w target
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>

#include "nfc_eeprom_mngt.h"
#include "tagtype5_wrapper.h"
#include "lib_NDEF_URI.h"
#include "lib_NDEF_Text.h"
#include "ctor10-w_data.h"
/* USER CODE END Includes */

/* USER CODE BEGIN Private defines */

#define EEPROM_VERSION_NAME   0x01  // version de la struct NAME
#define EEPROM_VERSION_CONFIG 0x01  // version de la struct CONFIG
#define ENDZ1            0x06
#define ENDZ2            0x07
#define ENDZ3            0x0F

#define START_NAME_ZONE   0xE0
#define START_CONFIG_ZONE 0x100
#define START_PUBLIC_ZONE 0x140

#define PROVISION_TIMEOUT_DEFAULT_S 120U
#define CFG_RFU_PROVISION_FLAG_IDX        0U
#define CFG_RFU_PROVISION_TIMEOUT_LSB_IDX 1U
#define CFG_RFU_PROVISION_TIMEOUT_MSB_IDX 2U
#define CFG_RFU_PROVISION_REASON_IDX      3U
#define CFG_RFU_PROVISION_RESULT_IDX      4U

#define I2C_LSB_PWD       0x00000000U
#define I2C_MSB_PWD       0x00000000U
/* USER CODE END Private defines */

/* USER CODE BEGIN Private variables */

Eeprom_name_Config_t  Eeprom_name_Config;
Eeprom_mngt_Config_t  Eeprom_mngt_Config;
Eeprom_mngt_Public_t  Eeprom_mngt_Public;

uint8_t  I2C_txBuf[TXBUFLEN];
uint8_t  I2C_rxBuf[TXBUFLEN];

sURI_Info             URIK;
ST25DVxxKC_PASSWD_t   PassWord;

uint64_t              MacAdd;
static bool           ProvisioningRuntimeSessionActive;
static uint32_t       ProvisioningRuntimeSessionDeadlineTick;
/* USER CODE END Private variables */

/* USER CODE BEGIN Private function prototypes */
static void EepromNameInitialisation(void);
static void EepromMngtInitialisation(void);
static void macAdd2string(uint64_t mac, char *str, size_t str_size);
static uint64_t BuildMacAdd(void);
static void WriteMngtConfigToEeprom(void);
/* USER CODE END Private function prototypes */

/* -------------------------------------------------------------------------- */
/*                          Fonctions publiques                               */
/* -------------------------------------------------------------------------- */

bool IsEepromAlreadyInitialised(void)
{
  // Ancienne API: tu peux la faire �voluer si besoin, pour l'instant true
  return true;
}

void UpdateDesignWithEEpromValue(void)
{
  // Met � jour la logique applicative � partir de l�EEPROM
  SetResetTimerFinish(Eeprom_mngt_Config.ResetTimerFinish);
  SetResetTimerNotFinish(Eeprom_mngt_Config.ResetTimerNotFinish);
}

/**
 * @brief  Initialisation globale NFC + EEPROM
 */
void EepromNFCInit(void)
{
  /* Init ST25DVXXKC driver */
  while (NFC07A1_NFCTAG_Init(NFC07A1_NFCTAG_INSTANCE) != NFCTAG_OK);

  /* Reset Mailbox enable to allow write to EEPROM */
  NFC07A1_NFCTAG_ResetMBEN_Dyn(NFC07A1_NFCTAG_INSTANCE);

  NfcTag_SelectProtocol(NFCTAG_TYPE5);

  // Prepare password
  PassWord.LsbPasswd = I2C_LSB_PWD;
  PassWord.MsbPasswd = I2C_MSB_PWD;

  uint32_t ret;
  bool bWriteEndZone = false;
  uint8_t u8tmp;

  // Check EndZone configuration
  ret = NFC07A1_NFCTAG_ReadEndZonex(NFC07A1_NFCTAG_INSTANCE,
                                    ST25DVXXKC_ZONE_END1,
                                    &u8tmp);
  if (u8tmp != ENDZ1)
  {
    bWriteEndZone = true;
  }

  ret = NFC07A1_NFCTAG_ReadEndZonex(NFC07A1_NFCTAG_INSTANCE,
                                    ST25DVXXKC_ZONE_END2,
                                    &u8tmp);
  if (u8tmp != ENDZ2)
  {
    bWriteEndZone = true;
  }

  ret = NFC07A1_NFCTAG_ReadEndZonex(NFC07A1_NFCTAG_INSTANCE,
                                    ST25DVXXKC_ZONE_END3,
                                    &u8tmp);
  if (u8tmp != ENDZ3)
  {
    bWriteEndZone = true;
  }

  if (bWriteEndZone == true)
  {
    ret = NFC07A1_NFCTAG_PresentI2CPassword(NFC07A1_NFCTAG_INSTANCE, PassWord);
    ret = NFC07A1_NFCTAG_InitEndZone(NFC07A1_NFCTAG_INSTANCE);
    ret = NFC07A1_NFCTAG_WriteEndZonex(NFC07A1_NFCTAG_INSTANCE,
                                       ST25DVXXKC_ZONE_END1, ENDZ1);
    ret = NFC07A1_NFCTAG_WriteEndZonex(NFC07A1_NFCTAG_INSTANCE,
                                       ST25DVXXKC_ZONE_END2, ENDZ2);
    ret = NFC07A1_NFCTAG_WriteEndZonex(NFC07A1_NFCTAG_INSTANCE,
                                       ST25DVXXKC_ZONE_END3, ENDZ3);
  }

  // Init EEPROM zones
  EepromConfigInitialisation();
  EepromPublicInitialisation();

  // Protection des zones NFC
  ST25DVxxKC_LOCK_STATUS_E tmpLockStatus;
  NFC07A1_NFCTAG_ReadLockCFG(NFC07A1_NFCTAG_INSTANCE, &tmpLockStatus);
  if (tmpLockStatus == ST25DVXXKC_UNLOCKED)
  {
    ret = NFC07A1_NFCTAG_PresentI2CPassword(NFC07A1_NFCTAG_INSTANCE, PassWord);

    ST25DVxxKC_RF_PROT_ZONE_t TmpProtZone;

    TmpProtZone.PasswdCtrl   = ST25DVXXKC_PROT_PASSWD1;
    TmpProtZone.RWprotection = ST25DVXXKC_WRITE_PROT;
    ret = NFC07A1_NFCTAG_WriteRFZxSS(NFC07A1_NFCTAG_INSTANCE,
                                     ST25DVXXKC_PROT_ZONE1,
                                     TmpProtZone);

    TmpProtZone.PasswdCtrl   = ST25DVXXKC_PROT_PASSWD2;
    TmpProtZone.RWprotection = ST25DVXXKC_WRITE_PROT;
    ret = NFC07A1_NFCTAG_WriteRFZxSS(NFC07A1_NFCTAG_INSTANCE,
                                     ST25DVXXKC_PROT_ZONE2,
                                     TmpProtZone);

    TmpProtZone.PasswdCtrl   = ST25DVXXKC_PROT_PASSWD3;
    TmpProtZone.RWprotection = ST25DVXXKC_WRITE_PROT;
    ret = NFC07A1_NFCTAG_WriteRFZxSS(NFC07A1_NFCTAG_INSTANCE,
                                     ST25DVXXKC_PROT_ZONE3,
                                     TmpProtZone);

    NFC07A1_NFCTAG_WriteLockCFG(NFC07A1_NFCTAG_INSTANCE, ST25DVXXKC_LOCKED);
  }
}

/**
 * @brief  V�rifie si le NDEF est coh�rent avec SSID + MAC
 */
bool isNDEFValid(void)
{
  uint16_t NDEF_length;
  sRecordInfo_t RecordStruct;
  char expected_text[30];
  char mac_str[18];
  uint8_t *pNDEF = NDEF_Buffer;

  if (NfcType5_GetLength(&NDEF_length) != NDEF_OK)
  {
    return false;
  }
  if (NDEF_length == 0)
  {
    return false;
  }
  if (NDEF_ReadNDEF(NDEF_Buffer) != NDEF_OK)
  {
    return false;
  }

  NDEF_IdentifyBuffer(&RecordStruct, pNDEF);
  pNDEF += RecordStruct.PayloadOffset + RecordStruct.PayloadLength;

  NDEF_IdentifyBuffer(&RecordStruct, pNDEF);
  if (RecordStruct.NDEF_Type != TEXT_TYPE)
  {
    return false;
  }

  macAdd2string(MacAdd, mac_str, sizeof(mac_str));
  snprintf(expected_text, sizeof(expected_text), "\002en%s_%s",
           Eeprom_name_Config.SSID, mac_str);

  if (RecordStruct.PayloadLength != strlen(expected_text))
  {
    return false;
  }
  return (memcmp(RecordStruct.PayloadBufferAdd,
                 expected_text,
                 RecordStruct.PayloadLength) == 0);
}

/**
 * @brief  Initialisation des zones NAME + MNGT + NDEF
 */
void EepromConfigInitialisation(void)
{
  // MAC globale pour la construction du texte NDEF
  MacAdd = BuildMacAdd();

  // 1) NAME: SSID + CRC
  EepromNameInitialisation();

  // 2) MNGT: ResetTimers / BLE / Mode + CRC
  EepromMngtInitialisation();

  // 3) NDEF
  if (!isNDEFValid())
  {
    CCFileStruct.MagicNumber = NFCT5_MAGICNUMBER_E1_CCFILE;
    CCFileStruct.Version     = NFCT5_VERSION_V1_0;
    CCFileStruct.MemorySize  = (ST25DVXXKC_MAX_SIZE / 8) & 0xFF;
    CCFileStruct.TT5Tag      = 0x05;

    while (NfcType5_TT5Init() != NFCTAG_OK);

    // URI record
    strcpy(URIK.protocol, URI_ID_0x01_STRING);
    strcpy(URIK.URI_Message, "kiwiprecision.fr/6-biathlon-laser-no-risk");
    strcpy(URIK.Information, "\0");
    while (NDEF_WriteURI(&URIK) != NDEF_OK);

    // Text record (SSID + MAC)
    char mac_str[18];
    char tbuf[30];
    macAdd2string(MacAdd, mac_str, sizeof(mac_str));
    snprintf(tbuf, sizeof(tbuf), "\002en%s_%s",
             Eeprom_name_Config.SSID, mac_str);

    sRecordInfo_t Textrecord;
    Textrecord.RecordFlags = SR_Mask | TNF_WellKnown;
    Textrecord.TypeLength  = TEXT_TYPE_STRING_LENGTH;
    memcpy(Textrecord.Type, TEXT_TYPE_STRING, TEXT_TYPE_STRING_LENGTH);
    Textrecord.PayloadBufferAdd = (uint8_t *)tbuf;
    Textrecord.PayloadLength    = strlen(tbuf);
    Textrecord.NDEF_Type        = TEXT_TYPE;

    while (NDEF_AppendRecord(&Textrecord) != NDEF_OK);
  }
}

/**
 * @brief  Initialisation zone PUBLIC (inchang�e)
 */
void EepromPublicInitialisation(void)
{
  uint32_t ret;
  Eeprom_mngt_Public_t Eeprom_mngt_Public_tmp = {0};
  int i;
  uint32_t CRCValue;

  ret = NFC07A1_NFCTAG_ReadData(NFC07A1_NFCTAG_INSTANCE,
                                (uint8_t *)&Eeprom_mngt_Public,
                                START_PUBLIC_ZONE,
                                sizeof(Eeprom_mngt_Public));
  (void)ret;

  CRCValue = HAL_CRC_Calculate(&hcrc,
                               (uint32_t *)&Eeprom_mngt_Public,
                               (uint32_t)&(((Eeprom_mngt_Public_t *)NULL)->Crc));
  if (Eeprom_mngt_Public.Crc != CRCValue)
  {
    Eeprom_mngt_Public_tmp.NBR_OF_ATTCHED_CLIENT = 0x00;
    for (i = 0; i < MAX_NBR_OF_ATTCHED_CLIENT; i++)
    {
      Eeprom_mngt_Public_tmp.CLIENT_ATTACHED_LIST[i] = 0;
    }

    NFC07A1_NFCTAG_PresentI2CPassword(NFC07A1_NFCTAG_INSTANCE, PassWord);
    Eeprom_mngt_Public_tmp.Crc = HAL_CRC_Calculate(
        &hcrc,
        (uint32_t *)&Eeprom_mngt_Public_tmp,
        (uint32_t)&(((Eeprom_mngt_Public_t *)NULL)->Crc));

    NFC07A1_NFCTAG_WriteData(NFC07A1_NFCTAG_INSTANCE,
                             (uint8_t *)&Eeprom_mngt_Public_tmp,
                             START_PUBLIC_ZONE,
                             sizeof(Eeprom_mngt_Public_tmp));

    memcpy(&Eeprom_mngt_Public,
           &Eeprom_mngt_Public_tmp,
           sizeof(Eeprom_mngt_Public));
  }
}

/* ------------ Getters / Setters publics ---------------------------------- */

uint64_t GetMacAdd(void)
{
  return MacAdd;
}

/* SSID */
char * GetSSIDName(void)
{
  return Eeprom_name_Config.SSID;
}

void SetSSIDName(char *SSIDNext)
{
  if (SSIDNext == NULL)
    return;

  if (strncmp(Eeprom_name_Config.SSID,
              SSIDNext,
              sizeof(Eeprom_name_Config.SSID)) != 0)
  {
    memset(Eeprom_name_Config.SSID, 0, sizeof(Eeprom_name_Config.SSID));
    strncpy(Eeprom_name_Config.SSID,
            SSIDNext,
            sizeof(Eeprom_name_Config.SSID) - 1);

    Eeprom_name_Config.Crc = HAL_CRC_Calculate(
        &hcrc,
        (uint32_t *)&Eeprom_name_Config,
        (uint32_t)&(((Eeprom_name_Config_t *)NULL)->Crc));

    while (NFC07A1_NFCTAG_WriteData(
               NFC07A1_NFCTAG_INSTANCE,
               (uint8_t *)&Eeprom_name_Config,
               START_NAME_ZONE,
               sizeof(Eeprom_name_Config_t)) != NFCTAG_OK)
    {
    }
  }
}

/* Reset timer finish */
uint8_t GetResetTimerValFinish(void)
{
  return Eeprom_mngt_Config.ResetTimerFinish;
}

void SetResetTimerValFinish(uint8_t ResetTimerFinish)
{
  if (ResetTimerFinish != Eeprom_mngt_Config.ResetTimerFinish)
  {
    Eeprom_mngt_Config.ResetTimerFinish = ResetTimerFinish;
    SetResetTimerFinish(ResetTimerFinish);

    Eeprom_mngt_Config.Crc = HAL_CRC_Calculate(
        &hcrc,
        (uint32_t *)&Eeprom_mngt_Config,
        (uint32_t)&(((Eeprom_mngt_Config_t *)NULL)->Crc));

    NFC07A1_NFCTAG_WriteData(NFC07A1_NFCTAG_INSTANCE,
                             (uint8_t *)&Eeprom_mngt_Config,
                             START_CONFIG_ZONE,
                             sizeof(Eeprom_mngt_Config_t));
  }
}

/* Reset timer not finish */
uint8_t GetResetTimerValNotFinish(void)
{
  return Eeprom_mngt_Config.ResetTimerNotFinish;
}

void SetResetTimerValNotFinish(uint8_t ResetTimerNotFinish)
{
  if (ResetTimerNotFinish != Eeprom_mngt_Config.ResetTimerNotFinish)
  {
    Eeprom_mngt_Config.ResetTimerNotFinish = ResetTimerNotFinish;
    SetResetTimerNotFinish(ResetTimerNotFinish);

    Eeprom_mngt_Config.Crc = HAL_CRC_Calculate(
        &hcrc,
        (uint32_t *)&Eeprom_mngt_Config,
        (uint32_t)&(((Eeprom_mngt_Config_t *)NULL)->Crc));

    NFC07A1_NFCTAG_WriteData(NFC07A1_NFCTAG_INSTANCE,
                             (uint8_t *)&Eeprom_mngt_Config,
                             START_CONFIG_ZONE,
                             sizeof(Eeprom_mngt_Config_t));
  }
}

/* Mode */
uint8_t GetMode(void)
{
  return Eeprom_mngt_Config.Mode;
}

void SetMode(uint8_t Mode)
{
  if (Mode != Eeprom_mngt_Config.Mode)
  {
    Eeprom_mngt_Config.Mode = Mode;

    Eeprom_mngt_Config.Crc = HAL_CRC_Calculate(
        &hcrc,
        (uint32_t *)&Eeprom_mngt_Config,
        (uint32_t)&(((Eeprom_mngt_Config_t *)NULL)->Crc));

    NFC07A1_NFCTAG_WriteData(NFC07A1_NFCTAG_INSTANCE,
                             (uint8_t *)&Eeprom_mngt_Config,
                             START_CONFIG_ZONE,
                             sizeof(Eeprom_mngt_Config_t));
  }
}

bool IsProvisioningBootRequested(void)
{
  return (Eeprom_mngt_Config.RFU[CFG_RFU_PROVISION_FLAG_IDX] == PROVISION_BOOT_FLAG_ACTIVE);
}

uint16_t GetProvisioningTimeoutSeconds(void)
{
  uint8_t lsb = Eeprom_mngt_Config.RFU[CFG_RFU_PROVISION_TIMEOUT_LSB_IDX];
  uint8_t msb = Eeprom_mngt_Config.RFU[CFG_RFU_PROVISION_TIMEOUT_MSB_IDX];
  uint16_t timeout = (uint16_t)lsb | ((uint16_t)msb << 8);

  if ((lsb == 0xFFU) && (msb == 0xFFU))
  {
    return PROVISION_TIMEOUT_DEFAULT_S;
  }

  if (timeout == 0U)
  {
    return PROVISION_TIMEOUT_DEFAULT_S;
  }

  return timeout;
}

uint8_t GetProvisioningReason(void)
{
  uint8_t reason = Eeprom_mngt_Config.RFU[CFG_RFU_PROVISION_REASON_IDX];
  return (reason == 0xFFU) ? PROVISION_REASON_NONE : reason;
}

uint8_t GetProvisioningResult(void)
{
  uint8_t result = Eeprom_mngt_Config.RFU[CFG_RFU_PROVISION_RESULT_IDX];
  return (result == 0xFFU) ? PROVISION_RESULT_UNKNOWN : result;
}

void SetProvisioningBootRequest(uint16_t timeoutSeconds, uint8_t reason)
{
  if (timeoutSeconds == 0U)
  {
    timeoutSeconds = PROVISION_TIMEOUT_DEFAULT_S;
  }

  Eeprom_mngt_Config.RFU[CFG_RFU_PROVISION_FLAG_IDX] = PROVISION_BOOT_FLAG_ACTIVE;
  Eeprom_mngt_Config.RFU[CFG_RFU_PROVISION_TIMEOUT_LSB_IDX] = (uint8_t)(timeoutSeconds & 0xFFU);
  Eeprom_mngt_Config.RFU[CFG_RFU_PROVISION_TIMEOUT_MSB_IDX] = (uint8_t)((timeoutSeconds >> 8) & 0xFFU);
  Eeprom_mngt_Config.RFU[CFG_RFU_PROVISION_REASON_IDX] = reason;
  Eeprom_mngt_Config.RFU[CFG_RFU_PROVISION_RESULT_IDX] = PROVISION_RESULT_UNKNOWN;

  WriteMngtConfigToEeprom();
}

void ClearProvisioningBootRequest(uint8_t result)
{
  Eeprom_mngt_Config.RFU[CFG_RFU_PROVISION_FLAG_IDX] = PROVISION_BOOT_FLAG_NONE;
  Eeprom_mngt_Config.RFU[CFG_RFU_PROVISION_RESULT_IDX] = result;

  WriteMngtConfigToEeprom();
}

void StartProvisioningRuntimeSession(uint16_t timeoutSeconds)
{
  if (timeoutSeconds == 0U)
  {
    timeoutSeconds = PROVISION_TIMEOUT_DEFAULT_S;
  }

  ProvisioningRuntimeSessionActive = true;
  ProvisioningRuntimeSessionDeadlineTick = HAL_GetTick() + ((uint32_t)timeoutSeconds * 1000U);
}

bool IsProvisioningRuntimeSessionActive(void)
{
  return ProvisioningRuntimeSessionActive;
}

bool IsProvisioningRuntimeSessionTimedOut(void)
{
  if (!ProvisioningRuntimeSessionActive)
  {
    return false;
  }

  return ((int32_t)(HAL_GetTick() - ProvisioningRuntimeSessionDeadlineTick) >= 0);
}

void StopProvisioningRuntimeSession(uint8_t result)
{
  if (!ProvisioningRuntimeSessionActive)
  {
    return;
  }

  ProvisioningRuntimeSessionActive = false;
  ClearProvisioningBootRequest(result);
}

void readEepromContent(void)
{
  // Debug �ventuel
}

/* -------------------------------------------------------------------------- */
/*                          Fonctions priv�es                                 */
/* -------------------------------------------------------------------------- */

static void macAdd2string(uint64_t mac, char *str, size_t str_size)
{
  snprintf(str, str_size, "%02X:%02X:%02X:%02X:%02X:%02X",
           (uint8_t)(mac >> 40) & 0xFF,
           (uint8_t)(mac >> 32) & 0xFF,
           (uint8_t)(mac >> 24) & 0xFF,
           (uint8_t)(mac >> 16) & 0xFF,
           (uint8_t)(mac >> 8) & 0xFF,
           (uint8_t)(mac) & 0xFF);
}

static uint64_t BuildMacAdd(void)
{
  uint64_t MacAddTmp = 0;
  MacAddTmp = 0xC000; /* The two upper bits shall be set to 1 to static random address */
  MacAddTmp = (MacAddTmp << 32) + LL_FLASH_GetUDN();
  return MacAddTmp;
}

/**
 * @brief  Init zone NAME (SSID + Version + RFU + CRC)
 */
static void EepromNameInitialisation(void)
{
  Eeprom_name_Config_t name_tmp = {0};
  uint32_t CRCValue;

  while (NFC07A1_NFCTAG_ReadData(NFC07A1_NFCTAG_INSTANCE,
                                 (uint8_t *)&Eeprom_name_Config,
                                 START_NAME_ZONE,
                                 sizeof(Eeprom_name_Config_t)) != NFCTAG_OK)
  {
  }

  CRCValue = HAL_CRC_Calculate(&hcrc,
                               (uint32_t *)&Eeprom_name_Config,
                               (uint32_t)&(((Eeprom_name_Config_t *)NULL)->Crc));

  if (Eeprom_name_Config.Crc != CRCValue ||
      Eeprom_name_Config.VersionEeprom != EEPROM_VERSION_NAME)
  {
    memset(&name_tmp, 0, sizeof(name_tmp));
    snprintf(name_tmp.SSID, sizeof(name_tmp.SSID), "%s", "CTOR10W");
    name_tmp.VersionEeprom = EEPROM_VERSION_NAME;
    memset(name_tmp.RFU, 0xFF, sizeof(name_tmp.RFU));

    name_tmp.Crc = HAL_CRC_Calculate(&hcrc,
                                     (uint32_t *)&name_tmp,
                                     (uint32_t)&(((Eeprom_name_Config_t *)NULL)->Crc));

    while (NFC07A1_NFCTAG_PresentI2CPassword(NFC07A1_NFCTAG_INSTANCE,
                                             PassWord) != NFCTAG_OK)
    {
    }

    while (NFC07A1_NFCTAG_WriteData(NFC07A1_NFCTAG_INSTANCE,
                                    (uint8_t *)&name_tmp,
                                    START_NAME_ZONE,
                                    sizeof(name_tmp)) != NFCTAG_OK)
    {
    }

    memcpy(&Eeprom_name_Config, &name_tmp, sizeof(Eeprom_name_Config));
  }
}

/**
 * @brief  Init zone MNGT (ResetTimers / BLE / Mode + RFU + CRC)
 */
static void EepromMngtInitialisation(void)
{
  Eeprom_mngt_Config_t cfg_tmp = {0};
  uint32_t CRCValue;
  bool writeCfg = false;

  while (NFC07A1_NFCTAG_ReadData(NFC07A1_NFCTAG_INSTANCE,
                                 (uint8_t *)&Eeprom_mngt_Config,
                                 START_CONFIG_ZONE,
                                 sizeof(Eeprom_mngt_Config_t)) != NFCTAG_OK)
  {
  }

  CRCValue = HAL_CRC_Calculate(&hcrc,
                               (uint32_t *)&Eeprom_mngt_Config,
                               (uint32_t)&(((Eeprom_mngt_Config_t *)NULL)->Crc));

  if (Eeprom_mngt_Config.Crc != CRCValue ||
      Eeprom_mngt_Config.VersionEeprom != EEPROM_VERSION_CONFIG)
  {
    writeCfg = true;

    while (NFC07A1_NFCTAG_PresentI2CPassword(NFC07A1_NFCTAG_INSTANCE,
                                             PassWord) != NFCTAG_OK)
    {
    }

    memset(&cfg_tmp, 0, sizeof(cfg_tmp));
    cfg_tmp.VersionEeprom       = EEPROM_VERSION_CONFIG;
    cfg_tmp.ResetTimerFinish    = 0;
    cfg_tmp.ResetTimerNotFinish = 0;
    cfg_tmp.BleCongig           = BLE_CONFIG_SERVER;
    cfg_tmp.Mode                = MODE_SOLEMS;
    memset(cfg_tmp.RFU, 0xFF, sizeof(cfg_tmp.RFU));

    cfg_tmp.Crc = HAL_CRC_Calculate(&hcrc,
                                    (uint32_t *)&cfg_tmp,
                                    (uint32_t)&(((Eeprom_mngt_Config_t *)NULL)->Crc));

    memcpy(&Eeprom_mngt_Config, &cfg_tmp, sizeof(Eeprom_mngt_Config));
  }

  if (writeCfg)
  {
    while (NFC07A1_NFCTAG_WriteData(NFC07A1_NFCTAG_INSTANCE,
                                    (uint8_t *)&cfg_tmp,
                                    START_CONFIG_ZONE,
                                    sizeof(cfg_tmp)) != NFCTAG_OK)
    {
    }
  }
}

static void WriteMngtConfigToEeprom(void)
{
  Eeprom_mngt_Config.Crc = HAL_CRC_Calculate(
      &hcrc,
      (uint32_t *)&Eeprom_mngt_Config,
      (uint32_t)&(((Eeprom_mngt_Config_t *)NULL)->Crc));

  while (NFC07A1_NFCTAG_WriteData(
             NFC07A1_NFCTAG_INSTANCE,
             (uint8_t *)&Eeprom_mngt_Config,
             START_CONFIG_ZONE,
             sizeof(Eeprom_mngt_Config_t)) != NFCTAG_OK)
  {
  }
}
