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
#include "dbg_trace.h"
#include "mode_manager.h"
#include "ble_mesh.h"
#include "dis_app.h"
/* USER CODE END Includes */

/* USER CODE BEGIN Private defines */

#define EEPROM_VERSION_NAME   0x01  // version de la struct NAME
#define EEPROM_VERSION_CONFIG EEPROM_CONFIG_VERSION
#define ENDZ1            0x06
#define ENDZ2            0x07
#define ENDZ3            0x0F

#define START_NAME_ZONE   0xE0
#define START_CONFIG_ZONE 0x100
#define START_PUBLIC_ZONE 0x140
#define EEPROM_CAPACITY_BYTES 0x0200U
#define CONFIG_ZONE_END_EXCLUSIVE 0x0140U
#define EEPROM_IO_MAX_ATTEMPTS 3U
/* ST25DV04KC is 512 bytes (0x000..0x1FF). Keep rescue opcode in Zone 1 so it
 * remains RF-writable while zones 2/3 stay protected. */
#define START_BOOT_CTRL_ZONE NFC_EEPROM_ADDR_BOOT_RESCUE_OPCODE

#define PROVISION_TIMEOUT_DEFAULT_S 120U
#define TOEM_NDEF_TEXT_MAX_LENGTH 190U

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
static uint32_t ConfigCrc(const Eeprom_mngt_Config_t *config);
static bool EepromReadBounded(uint16_t address, void *data, uint16_t length);
static bool EepromWriteBounded(uint16_t address, const void *data, uint16_t length);
static void BuildDefaultConfig(Eeprom_mngt_Config_t *config);
static bool BuildNdefIdentityText(char *text, size_t capacity);
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

    /* Keep zone1 RF-writable for rescue opcode updates from phone NFC tools. */
    TmpProtZone.PasswdCtrl   = ST25DVXXKC_NOT_PROTECTED;
    TmpProtZone.RWprotection = ST25DVXXKC_NO_PROT;
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
  char expected_text[TOEM_NDEF_TEXT_MAX_LENGTH + 1U];

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

  NDEF_IdentifyBuffer(&RecordStruct, NDEF_Buffer);
  if (RecordStruct.NDEF_Type != TEXT_TYPE)
  {
    return false;
  }
  if (!BuildNdefIdentityText(expected_text, sizeof(expected_text)))
  {
    return false;
  }
  if ((RecordStruct.PayloadLength < 3U) ||
      ((RecordStruct.PayloadLength - 3U) != strlen(expected_text)))
  {
    return false;
  }
  return (memcmp(RecordStruct.PayloadBufferAdd + 3U,
                 expected_text,
                 RecordStruct.PayloadLength - 3U) == 0);
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

  if (!isNDEFValid()) EepromNdefRefresh();
}

void EepromNdefRefresh(void)
{
  char text[TOEM_NDEF_TEXT_MAX_LENGTH + 1U];
  if (!BuildNdefIdentityText(text, sizeof(text))) return;

  CCFileStruct.MagicNumber = NFCT5_MAGICNUMBER_E1_CCFILE;
  CCFileStruct.Version = NFCT5_VERSION_V1_0;
  CCFileStruct.MemorySize = (ST25DVXXKC_MAX_SIZE / 8) & 0xFF;
  CCFileStruct.TT5Tag = 0x05;
  if (NfcType5_TT5Init() != NFCTAG_OK) return;
  /* CC + TLV + short Text record remains below reserved address 0x00D8. */
  if ((strlen(text) + 13U) >= START_BOOT_CTRL_ZONE) return;
  (void)NDEF_WriteText(text);
}

/**
 * @brief  Initialisation zone PUBLIC (inchang�e)
 */
void EepromPublicInitialisation(void)
{
  /* Legacy structure is 248 bytes and would end at 0x0237 on a 0x0200-byte
   * ST25DV04KC. It has no consumer; keep only a zeroed RAM image. */
  memset(&Eeprom_mngt_Public, 0, sizeof(Eeprom_mngt_Public));
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
  Eeprom_name_Config_t next;
  if (SSIDNext == NULL)
    return;

  if (strncmp(Eeprom_name_Config.SSID,
              SSIDNext,
              sizeof(Eeprom_name_Config.SSID)) != 0)
  {
    next = Eeprom_name_Config;
    memset(next.SSID, 0, sizeof(next.SSID));
    strncpy(next.SSID,
            SSIDNext,
            sizeof(next.SSID) - 1);

    next.Crc = HAL_CRC_Calculate(
        &hcrc,
        (uint32_t *)&next,
        (uint32_t)&(((Eeprom_name_Config_t *)NULL)->Crc));

    if (EepromWriteBounded(START_NAME_ZONE, &next,
                           sizeof(Eeprom_name_Config_t)))
    {
      Eeprom_name_Config = next;
      EepromNdefRefresh();
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
    Eeprom_mngt_Config_t next = Eeprom_mngt_Config;
    next.ResetTimerFinish = ResetTimerFinish;
    if (EepromConfigCommit(&next) == EEPROM_STORE_OK)
    {
      SetResetTimerFinish(ResetTimerFinish);
    }
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
    Eeprom_mngt_Config_t next = Eeprom_mngt_Config;
    next.ResetTimerNotFinish = ResetTimerNotFinish;
    if (EepromConfigCommit(&next) == EEPROM_STORE_OK)
    {
      SetResetTimerNotFinish(ResetTimerNotFinish);
    }
  }
}

/* Mode */
uint8_t GetMode(void)
{
  return Eeprom_mngt_Config.Mode;
}

void SetMode(uint8_t Mode)
{
  if (((Mode == MODE_SOLEMS) || (Mode == MODE_PSD)) &&
      (Mode != Eeprom_mngt_Config.Mode))
  {
    Eeprom_mngt_Config_t next = Eeprom_mngt_Config;
    next.Mode = Mode;
    next.SurfaceKind = (Mode == MODE_PSD) ? SURFACE_KIND_DYNAMIC : SURFACE_KIND_FULL;
    (void)EepromConfigCommit(&next);
  }
}

bool IsProvisioningBootRequested(void)
{
  return (Eeprom_mngt_Config.ProvisionBootFlag == PROVISION_BOOT_FLAG_ACTIVE);
}

uint16_t GetProvisioningTimeoutSeconds(void)
{
  uint8_t lsb = Eeprom_mngt_Config.ProvisionTimeoutSecondsLsb;
  uint8_t msb = Eeprom_mngt_Config.ProvisionTimeoutSecondsMsb;
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
  uint8_t reason = Eeprom_mngt_Config.ProvisionReason;
  return (reason == 0xFFU) ? PROVISION_REASON_NONE : reason;
}

uint8_t GetProvisioningResult(void)
{
  uint8_t result = Eeprom_mngt_Config.ProvisionResult;
  return (result == 0xFFU) ? PROVISION_RESULT_UNKNOWN : result;
}

void SetProvisioningBootRequest(uint16_t timeoutSeconds, uint8_t reason)
{
  Eeprom_mngt_Config_t next;
  if (timeoutSeconds == 0U)
  {
    timeoutSeconds = PROVISION_TIMEOUT_DEFAULT_S;
  }

  APP_ESSENTIAL_MSG("[EEPROM][MNGT] SetProvisioningBootRequest req: timeout=%u reason=%u\r\n",
              timeoutSeconds,
              reason);

  next = Eeprom_mngt_Config;
  next.ProvisionBootFlag = PROVISION_BOOT_FLAG_ACTIVE;
  next.ProvisionTimeoutSecondsLsb = (uint8_t)(timeoutSeconds & 0xFFU);
  next.ProvisionTimeoutSecondsMsb = (uint8_t)((timeoutSeconds >> 8) & 0xFFU);
  next.ProvisionReason = reason;
  next.ProvisionResult = PROVISION_RESULT_UNKNOWN;
  (void)EepromConfigCommit(&next);
}

void ClearProvisioningBootRequest(uint8_t result)
{
  Eeprom_mngt_Config_t next = Eeprom_mngt_Config;
  APP_ESSENTIAL_MSG("[EEPROM][MNGT] ClearProvisioningBootRequest req: result=%u\r\n", result);

  next.ProvisionBootFlag = PROVISION_BOOT_FLAG_NONE;
  next.ProvisionResult = result;
  if (EepromConfigCommit(&next) == EEPROM_STORE_OK) EepromNdefRefresh();
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

uint8_t GetNfcBootOpcode(void)
{
  uint8_t opcode = NFC_BOOT_OPCODE_NONE;

  if (NFC07A1_NFCTAG_ReadData(NFC07A1_NFCTAG_INSTANCE,
                              &opcode,
                              START_BOOT_CTRL_ZONE,
                              1U) != NFCTAG_OK)
  {
    return NFC_BOOT_OPCODE_NONE;
  }

  return opcode;
}

void SetNfcBootOpcode(uint8_t opcode)
{
  (void)EepromWriteBounded(START_BOOT_CTRL_ZONE, &opcode, 1U);
}

void ClearNfcBootOpcode(void)
{
  SetNfcBootOpcode(NFC_BOOT_OPCODE_NONE);
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

static uint16_t ReadConfigLe16(const uint8_t *value)
{
  return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static uint32_t ReadConfigLe32(const uint8_t *value)
{
  return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
         ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static uint64_t ReadConfigLe64(const uint8_t *value)
{
  uint64_t result = 0U;
  uint8_t index;
  for (index = 0U; index < 8U; index++) result |= ((uint64_t)value[index] << (8U * index));
  return result;
}

static bool BuildNdefIdentityText(char *text, size_t capacity)
{
  ModeStatus_t mode_status;
  const char *target_type;
  const char *surface;
  const char *mount;
  const char *comm_mode;
  uint32_t asset_id = ReadConfigLe32(Eeprom_mngt_Config.AssetIdLe);
  uint16_t owner_id = ReadConfigLe16(Eeprom_mngt_Config.OwnerIdLe);
  uint16_t site_id = ReadConfigLe16(Eeprom_mngt_Config.SiteIdLe);
  uint64_t network_id = ReadConfigLe64(Eeprom_mngt_Config.DestMeshNetworkIdLe);
  uint16_t mesh_address = 0U;
  int written;

  if ((text == NULL) || (capacity == 0U)) return false;
  if (asset_id == 0U) asset_id = (uint32_t)MacAdd;
  ModeManager_GetStatus(&mode_status);
  if (GetProvisioningResult() == PROVISION_RESULT_SUCCESS)
    mesh_address = (uint16_t)BLEMesh_GetAddress();

  target_type = (Eeprom_mngt_Config.Mode == MODE_PSD) ? "PSD" : "SOLEMS";
  surface = (Eeprom_mngt_Config.SurfaceKind == SURFACE_KIND_DYNAMIC) ? "DYN" :
            (Eeprom_mngt_Config.SurfaceKind == SURFACE_KIND_SHUTTERED) ? "SHUT" : "FULL";
  mount = (Eeprom_mngt_Config.MountPosition == MOUNT_POSITION_UPPER) ? "UP" :
          (Eeprom_mngt_Config.MountPosition == MOUNT_POSITION_LOWER) ? "LOW" : "SINGLE";
  switch (mode_status.actual_mode)
  {
    case BOOT_MODE_MESH_PROVISIONING: comm_mode = "PROV"; break;
    case BOOT_MODE_MESH_OPERATIONAL: comm_mode = "MESH"; break;
    case BOOT_MODE_GATT_MAINTENANCE: comm_mode = "MAINT"; break;
    case BOOT_MODE_GATT_RECOVERY: comm_mode = "RECOVERY"; break;
    default: comm_mode = "GATT"; break;
  }

  written = snprintf(text, capacity,
      "TOEM;A=%08lX;N=%s;O=%04X/%s;S=%04X/%s;D=%016" PRIX64 "/%s;L=%u-%s;T=%s;SF=%s;C=%s;P=%s;M=%04X;F=%s",
      (unsigned long)asset_id, Eeprom_name_Config.SSID,
      owner_id, Eeprom_mngt_Config.OwnerLabel,
      site_id, Eeprom_mngt_Config.SiteLabel,
      network_id, Eeprom_mngt_Config.NetworkLabel,
      Eeprom_mngt_Config.ShotLineNbr, mount, target_type, surface, comm_mode,
      (GetProvisioningResult() == PROVISION_RESULT_SUCCESS) ? "YES" : "NO",
      mesh_address, DISAPP_FIRMWARE_REVISION_NUMBER);
  return ((written >= 0) && ((size_t)written < capacity) &&
          ((size_t)written <= TOEM_NDEF_TEXT_MAX_LENGTH));
}

/**
 * @brief  Init zone NAME (SSID + Version + RFU + CRC)
 */
static void EepromNameInitialisation(void)
{
  Eeprom_name_Config_t name_tmp = {0};
  uint32_t CRCValue;

  if (!EepromReadBounded(START_NAME_ZONE, &Eeprom_name_Config,
                         sizeof(Eeprom_name_Config_t)))
  {
    memset(&Eeprom_name_Config, 0xFF, sizeof(Eeprom_name_Config));
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

    (void)NFC07A1_NFCTAG_PresentI2CPassword(NFC07A1_NFCTAG_INSTANCE, PassWord);
    if (EepromWriteBounded(START_NAME_ZONE, &name_tmp, sizeof(name_tmp)))
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
  bool read_ok;

  read_ok = EepromReadBounded(START_CONFIG_ZONE,
                              &Eeprom_mngt_Config,
                              sizeof(Eeprom_mngt_Config));
  if (!read_ok)
  {
    memset(&Eeprom_mngt_Config, 0xFF, sizeof(Eeprom_mngt_Config));
  }

  CRCValue = HAL_CRC_Calculate(&hcrc,
                               (uint32_t *)&Eeprom_mngt_Config,
                               (uint32_t)&(((Eeprom_mngt_Config_t *)NULL)->Crc));

  APP_ESSENTIAL_MSG("[EEPROM][MNGT] boot read: ver=%u boot=0x%02x timeout=%u reason=%u result=%u crc_stored=0x%08lx crc_calc=0x%08lx\r\n",
              Eeprom_mngt_Config.VersionEeprom,
              Eeprom_mngt_Config.ProvisionBootFlag,
              (uint16_t)Eeprom_mngt_Config.ProvisionTimeoutSecondsLsb
                | ((uint16_t)Eeprom_mngt_Config.ProvisionTimeoutSecondsMsb << 8),
              Eeprom_mngt_Config.ProvisionReason,
              Eeprom_mngt_Config.ProvisionResult,
              (unsigned long)Eeprom_mngt_Config.Crc,
              (unsigned long)CRCValue);

  if ((!read_ok) || Eeprom_mngt_Config.Crc != CRCValue ||
      Eeprom_mngt_Config.VersionEeprom != EEPROM_VERSION_CONFIG ||
      !EepromConfigValidate(&Eeprom_mngt_Config))
  {
    APP_ESSENTIAL_MSG("[EEPROM][MNGT] invalid config -> reset defaults (ver=%u expected=%u, crc_ok=%u)\r\n",
                Eeprom_mngt_Config.VersionEeprom,
                EEPROM_VERSION_CONFIG,
                (Eeprom_mngt_Config.Crc == CRCValue) ? 1U : 0U);

    BuildDefaultConfig(&cfg_tmp);
    (void)EepromConfigCommit(&cfg_tmp);

    APP_ESSENTIAL_MSG("[EEPROM][MNGT] defaults written: boot=0x%02x timeout=%u reason=%u result=%u\r\n",
                cfg_tmp.ProvisionBootFlag,
                (uint16_t)cfg_tmp.ProvisionTimeoutSecondsLsb
                  | ((uint16_t)cfg_tmp.ProvisionTimeoutSecondsMsb << 8),
                cfg_tmp.ProvisionReason,
                cfg_tmp.ProvisionResult);
  }
}

static uint32_t ConfigCrc(const Eeprom_mngt_Config_t *config)
{
  return HAL_CRC_Calculate(&hcrc, (uint32_t *)config,
                           (uint32_t)offsetof(Eeprom_mngt_Config_t, Crc));
}

static bool EepromRangeIsValid(uint16_t address, uint16_t length)
{
  uint32_t end = (uint32_t)address + (uint32_t)length;
  return ((length > 0U) && (end <= EEPROM_CAPACITY_BYTES));
}

static bool EepromReadBounded(uint16_t address, void *data, uint16_t length)
{
  uint32_t attempt;
  if ((data == NULL) || !EepromRangeIsValid(address, length)) return false;
  for (attempt = 0U; attempt < EEPROM_IO_MAX_ATTEMPTS; attempt++)
  {
    if (NFC07A1_NFCTAG_ReadData(NFC07A1_NFCTAG_INSTANCE, data, address, length) == NFCTAG_OK)
      return true;
  }
  return false;
}

static bool EepromWriteBounded(uint16_t address, const void *data, uint16_t length)
{
  uint32_t attempt;
  if ((data == NULL) || !EepromRangeIsValid(address, length)) return false;
  for (attempt = 0U; attempt < EEPROM_IO_MAX_ATTEMPTS; attempt++)
  {
    if (NFC07A1_NFCTAG_WriteData(NFC07A1_NFCTAG_INSTANCE,
                                 (uint8_t *)data, address, length) == NFCTAG_OK)
      return true;
  }
  return false;
}

static void BuildDefaultConfig(Eeprom_mngt_Config_t *config)
{
  memset(config, 0, sizeof(*config));
  config->VersionEeprom = EEPROM_VERSION_CONFIG;
  config->BleCongig = BLE_CONFIG_SERVER;
  config->Mode = MODE_SOLEMS;
  config->ProvisionTimeoutSecondsLsb = (uint8_t)(PROVISION_TIMEOUT_DEFAULT_S & 0xFFU);
  config->ProvisionTimeoutSecondsMsb = (uint8_t)(PROVISION_TIMEOUT_DEFAULT_S >> 8);
  config->ProvisionResult = PROVISION_RESULT_UNKNOWN;
  config->MountPosition = MOUNT_POSITION_SINGLE;
  config->SurfaceKind = SURFACE_KIND_FULL;
}

bool EepromConfigValidate(const Eeprom_mngt_Config_t *config)
{
  if ((config == NULL) || (config->VersionEeprom != EEPROM_VERSION_CONFIG)) return false;
  if ((config->Mode != MODE_SOLEMS) && (config->Mode != MODE_PSD)) return false;
  if (config->MountPosition > MOUNT_POSITION_LOWER) return false;
  if (config->SurfaceKind > SURFACE_KIND_SHUTTERED) return false;
  if ((config->Mode == MODE_PSD) != (config->SurfaceKind == SURFACE_KIND_DYNAMIC)) return false;
  if (config->ShotLineNbr == 0xFFU) return false;
  if (config->OwnerLabel[sizeof(config->OwnerLabel) - 1U] != '\0') return false;
  if (config->SiteLabel[sizeof(config->SiteLabel) - 1U] != '\0') return false;
  if (config->NetworkLabel[sizeof(config->NetworkLabel) - 1U] != '\0') return false;
  return true;
}

EepromStoreResult EepromConfigCommit(const Eeprom_mngt_Config_t *config)
{
  Eeprom_mngt_Config_t next;
  Eeprom_mngt_Config_t verify;

  if ((START_CONFIG_ZONE + sizeof(next) > CONFIG_ZONE_END_EXCLUSIVE) ||
      !EepromConfigValidate(config))
    return EEPROM_STORE_INVALID;

  next = *config;
  next.Crc = ConfigCrc(&next);
  if (!EepromWriteBounded(START_CONFIG_ZONE, &next, sizeof(next)))
    return EEPROM_STORE_IO_ERROR;
  if (!EepromReadBounded(START_CONFIG_ZONE, &verify, sizeof(verify)))
    return EEPROM_STORE_IO_ERROR;
  if ((memcmp(&next, &verify, sizeof(next)) != 0) || (verify.Crc != ConfigCrc(&verify)))
    return EEPROM_STORE_VERIFY_ERROR;

  Eeprom_mngt_Config = next;
  return EEPROM_STORE_OK;
}

void EepromConfigGetSnapshot(Eeprom_mngt_Config_t *config)
{
  if (config != NULL) *config = Eeprom_mngt_Config;
}
