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

typedef struct
{ 
	uint8_t VersionEeprom;
	uint8_t	ResetTimerFinish;
	uint8_t	ResetTimerNotFinish;
	uint8_t BleCongig; //can be BLE_CONFIG_CLIENT, BLE_CONFIG_SERVER, BLE_CONFIG_GATEWAY
	uint8_t Mode; //CTOR 0x01 PSD 0x02
	uint8_t RFU[19]; //19 to be aligned
	uint32_t	Crc; //must be placed at the end of typedef
}Eeprom_mngt_Config_t;

enum {
	MODE_NULL	  = 0,
	MODE_SOLEMS = 1,
	MODE_PSD = 2
};


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



#endif /* __NFC_EEPROM_MNGT_H */
