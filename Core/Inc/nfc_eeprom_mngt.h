/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * File Name          : App/nfc_eeprom_mngt.h
 * Description        : Header for ctor10-w_app_data.c module
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __NFC_EEPROM_MNGT_H
#define __NFC_EEPROM_MNGT_H
/*#ifdef __cplusplus
extern "C" {
#endif*/
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_NBR_OF_ATTCHED_CLIENT	30
enum {
	BLE_CONFIG_NULL			=0,
	BLE_CONFIG_CLIENT 	=1,
	BLE_CONFIG_SERVER 	=2,
	BLE_CONFIG_GATEWAY 	=3
};
	
#define SIZE_OF_SSID 8 // modulo 8
/*typedef struct
{ 
	char 		SSID[16];
	uint8_t VersionEeprom;
	uint8_t	ResetTimerFinish;
	uint8_t	ResetTimerNotFinish;
	uint8_t BleCongig; //can be BLE_CONFIG_CLIENT, BLE_CONFIG_SERVER, BLE_CONFIG_GATEWAY
	uint32_t	Crc; //must be placed at the end of typedef
}Eeprom_mngt_Config_t;*/

typedef struct
{ 
	char 		SSID[16];
	uint8_t VersionEeprom;
	uint8_t	RFU[3];
	uint32_t	Crc; //must be placed at the end of typedef
}Eeprom_name_Config_t;

#define EEPROM_CONFIG_VERSION             0x02U
#define EEPROM_CONFIG_SIZE                64U
#define EEPROM_CONFIG_OWNER_LABEL_SIZE     8U
#define EEPROM_CONFIG_SITE_LABEL_SIZE     10U
#define EEPROM_CONFIG_NETWORK_LABEL_SIZE  10U

enum {
	MOUNT_POSITION_SINGLE = 0,
	MOUNT_POSITION_UPPER  = 1,
	MOUNT_POSITION_LOWER  = 2
};

enum {
	SURFACE_KIND_DYNAMIC   = 0,
	SURFACE_KIND_FULL      = 1,
	SURFACE_KIND_SHUTTERED = 2
};

typedef struct
{ 
	uint8_t VersionEeprom;
	uint8_t	ResetTimerFinish;
	uint8_t	ResetTimerNotFinish;
	uint8_t BleCongig; //can be BLE_CONFIG_CLIENT, BLE_CONFIG_SERVER, BLE_CONFIG_GATEWAY
	uint8_t Mode; //CTOR 0x01 PSD 0x02
	uint8_t ProvisionBootFlag;
	uint8_t ProvisionTimeoutSecondsLsb;
	uint8_t ProvisionTimeoutSecondsMsb;
	uint8_t ProvisionReason;
	uint8_t ProvisionResult;
	uint8_t ShotLineNbr;
	uint8_t MountPosition;
	uint8_t SurfaceKind;
	uint8_t ConfigFlags;
	uint8_t AssetIdLe[4];
	uint8_t OwnerIdLe[2];
	uint8_t SiteIdLe[2];
	uint8_t DestMeshNetworkIdLe[8];
	uint8_t AssignmentGenerationLe[2];
	char OwnerLabel[EEPROM_CONFIG_OWNER_LABEL_SIZE];
	char SiteLabel[EEPROM_CONFIG_SITE_LABEL_SIZE];
	char NetworkLabel[EEPROM_CONFIG_NETWORK_LABEL_SIZE];
	uint32_t	Crc; //must be placed at the end of typedef
}Eeprom_mngt_Config_t;

typedef enum
{
	EEPROM_STORE_OK = 0,
	EEPROM_STORE_INVALID,
	EEPROM_STORE_IO_ERROR,
	EEPROM_STORE_VERIFY_ERROR
} EepromStoreResult;

enum {
	MODE_NULL	  = 0,
	MODE_SOLEMS = 1,
	MODE_PSD = 2
};

enum {
	PROVISION_REASON_NONE = 0,
	PROVISION_REASON_GATT = 1,
	PROVISION_REASON_NFC = 2
};

enum {
	PROVISION_RESULT_UNKNOWN = 0,
	PROVISION_RESULT_SUCCESS = 1,
	PROVISION_RESULT_FAIL = 2,
	PROVISION_RESULT_TIMEOUT = 3
};

#define PROVISION_BOOT_FLAG_NONE   0x00U
#define PROVISION_BOOT_FLAG_ACTIVE 0xA5U

/* NFC boot rescue opcode values (stored outside CRC-protected config struct). */
#define NFC_BOOT_OPCODE_NONE               0xFFU
#define NFC_BOOT_OPCODE_MESH_FACTORY_RESET 0xA5U

/* Byte address in ST25DV04KC EEPROM (Zone 1) used for one-shot rescue command.
 * 0x00D8 == 216 decimal, RF-writable for field recovery without a button. */
#define NFC_EEPROM_ADDR_BOOT_RESCUE_OPCODE 0x00D8U


typedef struct
{
	uint64_t CLIENT_ATTACHED_LIST[MAX_NBR_OF_ATTCHED_CLIENT];
	uint32_t	NBR_OF_ATTCHED_CLIENT;
	uint32_t	Crc; 
}Eeprom_mngt_Public_t;

extern Eeprom_name_Config_t  Eeprom_name_Config;
extern Eeprom_mngt_Config_t Eeprom_mngt_Config;
extern Eeprom_mngt_Public_t Eeprom_mngt_Public;

extern bool IsEepromAlreadyInitialised();
extern char * GetSSIDName();
extern void EepromConfigInitialisation();
extern void EepromPublicInitialisation();
extern void EepromPrivateInitialisation();
extern uint64_t GetMacAdd();
extern void SetSSIDName(char * SSIDNext);
extern uint8_t GetResetTimerValFinish();
extern void SetResetTimerValFinish(uint8_t ResetTimerValNext);
extern uint8_t GetResetTimerValNotFinish();
extern void SetResetTimerValNotFinish(uint8_t ResetTimerValNext);
extern uint8_t GetMode();
extern void SetMode(uint8_t Mode);
extern void EepromNFCInit();
extern void UpdateDesignWithEEpromValue();
extern bool IsProvisioningBootRequested(void);
extern uint16_t GetProvisioningTimeoutSeconds(void);
extern uint8_t GetProvisioningReason(void);
extern uint8_t GetProvisioningResult(void);
extern void SetProvisioningBootRequest(uint16_t timeoutSeconds, uint8_t reason);
extern void ClearProvisioningBootRequest(uint8_t result);
extern void StartProvisioningRuntimeSession(uint16_t timeoutSeconds);
extern bool IsProvisioningRuntimeSessionActive(void);
extern bool IsProvisioningRuntimeSessionTimedOut(void);
extern void StopProvisioningRuntimeSession(uint8_t result);
extern uint8_t GetNfcBootOpcode(void);
extern void SetNfcBootOpcode(uint8_t opcode);
extern void ClearNfcBootOpcode(void);
extern bool EepromConfigValidate(const Eeprom_mngt_Config_t *config);
extern EepromStoreResult EepromConfigCommit(const Eeprom_mngt_Config_t *config);
extern void EepromConfigGetSnapshot(Eeprom_mngt_Config_t *config);
extern void EepromNdefRefresh(void);

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(Eeprom_name_Config_t) == 24U, "NAME EEPROM layout changed");
_Static_assert(offsetof(Eeprom_name_Config_t, Crc) == 20U, "NAME CRC offset changed");
_Static_assert(sizeof(Eeprom_mngt_Config_t) == EEPROM_CONFIG_SIZE, "CONFIG EEPROM layout changed");
_Static_assert(offsetof(Eeprom_mngt_Config_t, Crc) == 60U, "CONFIG CRC offset changed");
#endif



#endif /* __NFC_EEPROM_MNGT_H */
