/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : ctor10-w_app_data.c
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
#include "stm32_lpm.h"
#include "stm32_seq.h"
#include "dbg_trace.h"
#include "hw_conf.h"
#include "otp.h"
#include <string.h>
#include <stdbool.h>
#include "bas_app.h"
#include "p2p_server_app.h"
#include "ctor10-w_data.h"
#include "appli_vendor.h"
#include "target_config_service.h"
#include <inttypes.h>
/* USER CODE END Includes */

 extern UART_HandleTypeDef 	hlpuart1;
 
#define ONE_S   (1 * 1000 * 1000 / CFG_TS_TICK_VAL)
#define SHOT_MEMORY_CAPACITY 1024U
typedef struct
 {
 	uint8_t 	Shot;
 	uint8_t 	Key;
 	uint8_t 	ShotNbr;
 	uint8_t 	MaxShotNbr;
 	uint8_t 	CarabineNbr[2];
 	uint8_t 	LoadingNbr[2];
 	uint8_t 	TargetData;
 	uint8_t 	XPos;
 	uint8_t 	YPos;

 	uint8_t 	CarabineFw;
 	uint8_t 	Temp;
 	uint8_t 	Pwr;
	 bool update_Pwr;
 	uint8_t 	AmbiantLight;
 	uint8_t 	TargetFw;
 	bool	JN;
 	bool 	EnergySaver;
 	bool    VisualState[5];
 	uint32_t AbsoluteTimeTick;
	uint8_t ShotMemory[SHOT_MEMORY_CAPACITY][NOTIFY_SHOT_SIZE];
	uint8_t VisualImpacted;
	uint16_t ShotMemoryCount;
	uint32_t ShotNextSequence;
 } DecodeRx_t;
 DecodeRx_t 			RXDecode;

bool bGPIOVisualStateEnable;
//uint8_t Tx_data[80] = {0x01};
char DISAPP_HARDWARE_REVISION_NUMBER[6];
typedef struct
{
  /**
   * ID of the Advertising Timeout
   */
  uint8_t Reset_mgr_timer_Id;

  uint8_t uiResetTimer;
}ResetTimer_t;
ResetTimer_t ResetTimerFinish;
ResetTimer_t ResetTimerNotFinish;
 
void DecodeTargetRx(uint8_t *Rx_data, int lenght)
{
	int i;
	int msk;
	RXDecode.AbsoluteTimeTick = HAL_GetTick();
	RXDecode.Shot = Rx_data[0];
	RXDecode.Key = Rx_data[1];
	RXDecode.ShotNbr = Rx_data[2];
	RXDecode.MaxShotNbr = Rx_data[3];
	memcpy (RXDecode.CarabineNbr,&Rx_data[4],2);
	memcpy (RXDecode.LoadingNbr,&Rx_data[6],2);
	RXDecode.CarabineFw = Rx_data[8];
	RXDecode.TargetData = Rx_data[10];
	for (i = 0; i<5;i++)
	{
		msk = (1<<i);
		RXDecode.VisualState[i] = (Rx_data[10] & msk) == msk;
	}
	msk = (1<<5);
	RXDecode.JN =  (Rx_data[10] & msk) == msk;
	msk = (1<<6);
	RXDecode.EnergySaver =  (Rx_data[10] & msk) == msk;
	RXDecode.XPos = Rx_data[11];
	RXDecode.YPos = Rx_data[12];
	RXDecode.Temp = Rx_data[13];
	if (RXDecode.Pwr != Rx_data[14])
	{
		RXDecode.Pwr = Rx_data[14];
		RXDecode.update_Pwr = TRUE;
	}
	RXDecode.AmbiantLight = Rx_data[15];
	RXDecode.TargetFw = Rx_data[16];
	RXDecode.VisualImpacted = Rx_data[18];
}
void SetGPIOVisualState()
{
	if (RXDecode.VisualState[0] == true)
	{
		//HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);//LED control
		HAL_GPIO_WritePin(led_target_1_GPIO_Port, led_target_1_Pin, GPIO_PIN_SET);
	}
	else
	{
		//HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);//LED control
		HAL_GPIO_WritePin(led_target_1_GPIO_Port, led_target_1_Pin, GPIO_PIN_RESET);
	}
	if (RXDecode.VisualState[1] == true)
	{
		//HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);//LED control
		HAL_GPIO_WritePin(led_target_2_GPIO_Port, led_target_2_Pin, GPIO_PIN_SET);
	}
	else
	{
		//HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);//LED control
		HAL_GPIO_WritePin(led_target_2_GPIO_Port, led_target_2_Pin, GPIO_PIN_RESET);
	}
	if (RXDecode.VisualState[2] == true)
	{
		//HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);//LED control
		HAL_GPIO_WritePin(led_target_3_GPIO_Port, led_target_3_Pin, GPIO_PIN_SET);
	}
	else
	{
		//HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET); //LED control
		//HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);
		HAL_GPIO_WritePin(led_target_3_GPIO_Port, led_target_3_Pin, GPIO_PIN_RESET);
	}
	if (RXDecode.VisualState[3] == true)
	{
		HAL_GPIO_WritePin(led_target_4_GPIO_Port, led_target_4_Pin, GPIO_PIN_SET);
	}
	else
	{
		HAL_GPIO_WritePin(led_target_4_GPIO_Port, led_target_4_Pin, GPIO_PIN_RESET);
	}
	if (RXDecode.VisualState[4] == true)
	{
		HAL_GPIO_WritePin(led_target_5_GPIO_Port, led_target_5_Pin, GPIO_PIN_SET);
	}
	else
	{
		HAL_GPIO_WritePin(led_target_5_GPIO_Port, led_target_5_Pin, GPIO_PIN_RESET);
	}
}


void GetHardwareVersion(void)
{
    int major = 0;
    int minor = 0;
    int patch = 0;

    if (GPIO_PIN_SET == HAL_GPIO_ReadPin(HW_Version_2_GPIO_Port, HW_Version_2_Pin))
    {
        major = 1;
    }

    if (GPIO_PIN_SET == HAL_GPIO_ReadPin(HW_Version_1_GPIO_Port, HW_Version_1_Pin))
    {
        minor = 1;
    }

    if (GPIO_PIN_SET == HAL_GPIO_ReadPin(HW_Version_0_GPIO_Port, HW_Version_0_Pin))
    {
        patch = 1;
    }

    // Mettre � jour la variable DISAPP_HARDWARE_REVISION_NUMBER
    sprintf(DISAPP_HARDWARE_REVISION_NUMBER, "%d.%d.%d", major, minor, patch);
}

void PrepareBLENotificationRaw()
{
	UTIL_SEQ_SetTask( 1<<CFG_TASK_RAW_NOTIFICATION_ID, CFG_SCH_PRIO_4);
}
void DebugPrintShotNotification(uint8_t *data)
{
    char buffer[64];
    char hex[6];   // "FF \0"

    APP_DBG_MSG("\r\n--- BLE SHOT NOTIFICATION ---\r\n");

    snprintf(buffer, sizeof(buffer), "CarabineNbr[0] : ");
    APP_DBG_MSG("%s", buffer);
    snprintf(hex, sizeof(hex), "%02X\r\n", data[0]);
    APP_DBG_MSG("%s", hex);

    snprintf(buffer, sizeof(buffer), "CarabineNbr[1] : ");
    APP_DBG_MSG("%s", buffer);
    snprintf(hex, sizeof(hex), "%02X\r\n", data[1]);
    APP_DBG_MSG("%s", hex);

    snprintf(buffer, sizeof(buffer), "ShotNbr        : ");
    APP_DBG_MSG("%s", buffer);
    snprintf(hex, sizeof(hex), "%02X\r\n", data[2]);
    APP_DBG_MSG("%s", hex);

    snprintf(buffer, sizeof(buffer), "TargetData     : ");
    APP_DBG_MSG("%s", buffer);
    snprintf(hex, sizeof(hex), "%02X\r\n", data[3]);
    APP_DBG_MSG("%s", hex);

    snprintf(buffer, sizeof(buffer), "XPos           : ");
    APP_DBG_MSG("%s", buffer);
    snprintf(hex, sizeof(hex), "%02X\r\n", data[4]);
    APP_DBG_MSG("%s", hex);

    snprintf(buffer, sizeof(buffer), "YPos           : ");
    APP_DBG_MSG("%s", buffer);
    snprintf(hex, sizeof(hex), "%02X\r\n", data[5]);
    APP_DBG_MSG("%s", hex);

    uint32_t time;
    memcpy(&time, &data[6], 4);
    snprintf(buffer, sizeof(buffer), "AbsoluteTimeTick : %" PRIu32 "\r\n", time);
    APP_DBG_MSG("%s", buffer);

    snprintf(buffer, sizeof(buffer),
             "VisualImpacted   : %02X\r\n",
             data[10]);
    APP_DBG_MSG("%s", buffer);

    snprintf(buffer, sizeof(buffer),
             "LoadingNbr    : %u\r\n",
             (unsigned int)data[11]);
    APP_DBG_MSG("%s", buffer);

    snprintf(buffer, sizeof(buffer),
             "MaxShotNbr    : %u\r\n",
             (unsigned int)data[12]);
    APP_DBG_MSG("%s", buffer);

    APP_DBG_MSG("Full Frame: ");
    for (int i = 0; i < NOTIFY_SHOT_SIZE; i++)
    {
        int len = snprintf(hex, sizeof(hex), "%02X ", data[i]);
        APP_DBG_MSG("%.*s", len, hex);
    }
    APP_DBG_MSG("\r\n-----------------------------\r\n");
}

uint8_t BLENotificationShot[NOTIFY_SHOT_SIZE];  //  creating a buffer of 100 bytes
void PrepareBLENotificationShot()
{
	bool BleNotificationShot = FALSE;
	if (BLENotificationShot[0]!= RXDecode.CarabineNbr[0])
	{
		BLENotificationShot[0] = RXDecode.CarabineNbr[0];
		BleNotificationShot = TRUE;
	}
	if (BLENotificationShot[1]!= RXDecode.CarabineNbr[1])
	{
		BLENotificationShot[1] = RXDecode.CarabineNbr[1];
		BleNotificationShot = TRUE;
	}
	if (BLENotificationShot[2]!= RXDecode.ShotNbr)
	{
		BLENotificationShot[2] = RXDecode.ShotNbr;
		BleNotificationShot = TRUE;
	}
	if (BLENotificationShot[3]!= RXDecode.TargetData)
	{
		BLENotificationShot[3] = RXDecode.TargetData;
		BleNotificationShot = TRUE;
	}
	if (BLENotificationShot[4]!= RXDecode.XPos)
	{
		BLENotificationShot[4] = RXDecode.XPos;
		BleNotificationShot = TRUE;
	}
	if (BLENotificationShot[5]!= RXDecode.YPos)
	{
		BLENotificationShot[5] = RXDecode.YPos;
		BleNotificationShot = TRUE;
	}
	if (BleNotificationShot == TRUE)
	{
		uint32_t shot_sequence;
		uint16_t shot_slot;

		memcpy (&BLENotificationShot[6],&RXDecode.AbsoluteTimeTick,4);
		BLENotificationShot[10] = RXDecode.VisualImpacted;
		BLENotificationShot[11] = RXDecode.LoadingNbr[1];
		if (BLENotificationShot[12]!= RXDecode.MaxShotNbr)
		{
			BLENotificationShot[12] = RXDecode.MaxShotNbr;
			BleNotificationShot = TRUE;
		}
		shot_sequence = RXDecode.ShotNextSequence;
		shot_slot = (uint16_t)(shot_sequence % SHOT_MEMORY_CAPACITY);
		memcpy(RXDecode.ShotMemory[shot_slot], BLENotificationShot, NOTIFY_SHOT_SIZE);
		RXDecode.ShotNextSequence++;
		if (RXDecode.ShotMemoryCount < SHOT_MEMORY_CAPACITY)
		{
			RXDecode.ShotMemoryCount++;
		}
		if (RXDecode.VisualState[0] == TRUE &&
				RXDecode.VisualState[1] == TRUE &&
				RXDecode.VisualState[2] == TRUE &&
				RXDecode.VisualState[3] == TRUE &&
				RXDecode.VisualState[4] == TRUE)	//reset only if one target is active
		{
			SetResetTimerFinish(ResetTimerFinish.uiResetTimer);		//if shot occur and all target true reset timer to wait Finish time before reset
		}
		else
		{
			SetResetTimerNotFinish(ResetTimerNotFinish.uiResetTimer);		//if shot occur reset timer to wait NotFinish time before reset
		}
		storeShotData(BLENotificationShot,NOTIFY_SHOT_SIZE);
		UTIL_SEQ_SetTask( 1<<CFG_TASK_SHOT_NOTIFICATION_ID, CFG_SCH_PRIO_1);
		UTIL_SEQ_SetTask( 1<<CFG_TASK_SHOT_ADV_ID, CFG_SCH_PRIO_2);
		Appli_Vendor_QueueShotEvent(BLENotificationShot, NOTIFY_SHOT_SIZE, shot_sequence);
	}
	//DebugPrintShotNotification(BLENotificationShot);
}
uint8_t BLENotificationTarget[NOTIFY_TARGET_SIZE];  //  creating a buffer of 100 bytes
void PrepareBLENotificationTarget()
{
	
	bool BleNotificationTarget = FALSE;
	if (BLENotificationTarget[0]!= RXDecode.JN)
	{
		BLENotificationTarget[0]= RXDecode.JN;
		BleNotificationTarget = TRUE;
	}
	if (BLENotificationTarget[1]!= RXDecode.EnergySaver)
	{
		BLENotificationTarget[1]= RXDecode.EnergySaver;
		BleNotificationTarget = TRUE;
	}
	if (BLENotificationTarget[2]!= RXDecode.Temp)
	{
		BLENotificationTarget[2]= RXDecode.Temp;
		BleNotificationTarget = TRUE;
	}
	if (BLENotificationTarget[3]!= RXDecode.AmbiantLight)
	{
		BLENotificationTarget[3]= RXDecode.AmbiantLight;
		BleNotificationTarget = TRUE;
	}
	if (BleNotificationTarget == TRUE)
	{
		storeTargetData(BLENotificationTarget,NOTIFY_TARGET_SIZE);
		UTIL_SEQ_SetTask( 1<<CFG_TASK_TARGET_NOTIFICATION_ID, CFG_SCH_PRIO_3);
		Appli_Vendor_QueueTargetEvent(BLENotificationTarget, NOTIFY_TARGET_SIZE);
	}
	
	if (RXDecode.update_Pwr == TRUE)
	{
		RXDecode.update_Pwr = FALSE;
		BASAPP_UpdateBatLevel(RXDecode.Pwr);
		Appli_Vendor_QueueBatteryEvent(RXDecode.Pwr);
	}
}

void ctor10wStoreAndNotify(uint8_t *Rx_data, int lenght)
{
	storeRawData(Rx_data,20);
	DecodeTargetRx(Rx_data,20);
	if (bGPIOVisualStateEnable == TRUE)
	{
		SetGPIOVisualState();
	}
	PrepareBLENotificationRaw();
	PrepareBLENotificationShot();
	PrepareBLENotificationTarget();
}

void GPIOVisualStateEnable()
{
	bGPIOVisualStateEnable = TRUE;
}
void GPIOVisualStateDisable()
{
	bGPIOVisualStateEnable = FALSE;
}

/*
**Reset procedures
*/
static void ResetTargetTimer(void)
{
	if (TargetRuntimeService_IsZeroingHoldEnabled() != 0U)
	{
		return;
	}
	if (RXDecode.VisualState[0] == TRUE ||
			RXDecode.VisualState[1] == TRUE ||
			RXDecode.VisualState[2] == TRUE ||
			RXDecode.VisualState[3] == TRUE ||
			RXDecode.VisualState[4] == TRUE)	//reset only if one target is active
	{
		uint8_t ResetCmd = 0x01;
		HAL_UART_Transmit(&hlpuart1,&ResetCmd,1,1000);
	}
}


/*
**Initialise structure and create Timer to automatic reset
*/
void ctor10w_data_Init(void)
{
	RXDecode.ShotMemoryCount = 0U;
	RXDecode.ShotNextSequence = 0U;
	HW_TS_Create(CFG_TIM_PROC_ID_ISR,&ResetTimerFinish.Reset_mgr_timer_Id,hw_ts_SingleShot,ResetTargetTimer);
	HW_TS_Create(CFG_TIM_PROC_ID_ISR,&ResetTimerNotFinish.Reset_mgr_timer_Id,hw_ts_SingleShot,ResetTargetTimer);
}

void SetResetTimerFinish(uint8_t FinishTimer)
{
	
	HW_TS_Stop(ResetTimerFinish.Reset_mgr_timer_Id);
	ResetTimerFinish.uiResetTimer= FinishTimer;
	if (ResetTimerFinish.uiResetTimer!=0x00) //if timer == x timer is inactive
	{
		uint32_t tmpTimer = ResetTimerFinish.uiResetTimer*ONE_S;
		HW_TS_Start(ResetTimerFinish.Reset_mgr_timer_Id, tmpTimer);
	}
}

void SetResetTimerNotFinish(uint8_t NotFinishTimer)
{
	
	HW_TS_Stop(ResetTimerNotFinish.Reset_mgr_timer_Id);
	ResetTimerNotFinish.uiResetTimer= NotFinishTimer;
	if (ResetTimerNotFinish.uiResetTimer!=0x00) //if timer == x timer is inactive
	{
		uint32_t tmpTimer = ResetTimerNotFinish.uiResetTimer*ONE_S;
		HW_TS_Start(ResetTimerNotFinish.Reset_mgr_timer_Id, tmpTimer);
	}
}

void GetTabShot(void)
{
	uint16_t i = 0U;
	for(i = 0U; i < RXDecode.ShotMemoryCount; i++)
	{
		//storeShotTabData(RXDecode.ShotMemory[0],NOTIFY_SHOT_SIZE);
		//UTIL_SEQ_SetTask( 1<<CFG_TASK_SHOT_NOTIFICATIONTAB_ID, CFG_SCH_PRIO_0);

		
	}
}

uint8_t GetLastShotNotification(uint8_t *dst, uint8_t max_len)
{
	uint8_t copy_len = NOTIFY_SHOT_SIZE;

	if ((dst == NULL) || (max_len == 0U))
	{
		return 0U;
	}

	if (copy_len > max_len)
	{
		copy_len = max_len;
	}

	memcpy(dst, BLENotificationShot, copy_len);
	return copy_len;
}

uint8_t GetLastTargetNotification(uint8_t *dst, uint8_t max_len)
{
	uint8_t copy_len = NOTIFY_TARGET_SIZE;

	if ((dst == NULL) || (max_len == 0U))
	{
		return 0U;
	}

	if (copy_len > max_len)
	{
		copy_len = max_len;
	}

	memcpy(dst, BLENotificationTarget, copy_len);
	return copy_len;
}

uint8_t GetLastBatteryLevel(void)
{
	return RXDecode.Pwr;
}

uint16_t GetShotQueueCount(void)
{
	return RXDecode.ShotMemoryCount;
}

uint32_t GetShotQueueOldestSequence(void)
{
	return RXDecode.ShotNextSequence - (uint32_t)RXDecode.ShotMemoryCount;
}

uint32_t GetShotQueueNextSequence(void)
{
	return RXDecode.ShotNextSequence;
}

bool GetShotBySequence(uint32_t sequence, uint8_t *dst, uint8_t max_len)
{
	uint32_t oldest_sequence;
	uint16_t slot;

	if ((dst == NULL) || (max_len < NOTIFY_SHOT_SIZE))
	{
		return false;
	}

	oldest_sequence = GetShotQueueOldestSequence();
	if ((sequence < oldest_sequence) || (sequence >= RXDecode.ShotNextSequence))
	{
		return false;
	}

	slot = (uint16_t)(sequence % SHOT_MEMORY_CAPACITY);
	memcpy(dst, RXDecode.ShotMemory[slot], NOTIFY_SHOT_SIZE);
	return true;
}
