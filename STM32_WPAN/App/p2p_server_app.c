/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    App/p2p_server_app.c
  * @author  MCD Application Team
  * @brief   Peer to peer Server Application
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "app_common.h"
#include "dbg_trace.h"
#include "ble.h"
#include "p2p_server_app.h"
#include "stm32_seq.h"
#include "nfc_eeprom_mngt.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
 extern UART_HandleTypeDef 	hlpuart1;
 #include "ctor10-w_data.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

 
 PLACE_IN_SECTION("BLE_APP_CONTEXT") P2P_Server_App_Context_t P2P_Server_App_Context;
/* USER CODE END PTD */

/* Private defines ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macros -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

static void P2PS_Send_Raw_Notification(void);
static void P2PS_Send_Shot_Notification(void);
static void P2PS_Send_Target_Notification(void);
uint8_t Tx_data[2];
	uint8_t tab_emul[14];
	uint8_t RetValToRead;
/* USER CODE END PFP */

/* Functions Definition ------------------------------------------------------*/
void P2PS_STM_App_Notification(P2PS_STM_App_Notification_evt_t *pNotification)
{
/* USER CODE BEGIN P2PS_STM_App_Notification_1 */

/* USER CODE END P2PS_STM_App_Notification_1 */
  switch(pNotification->P2P_Evt_Opcode)
  {
/* USER CODE BEGIN P2PS_STM_App_Notification_P2P_Evt_Opcode */
#if(BLE_CFG_OTA_REBOOT_CHAR != 0)
    case P2PS_STM_BOOT_REQUEST_EVT:
      APP_DBG_MSG("-- P2P APPLICATION SERVER : BOOT REQUESTED\n");
      APP_DBG_MSG(" \n\r");

      *(uint32_t*)SRAM1_BASE = *(uint32_t*)pNotification->DataTransfered.pPayload;
      NVIC_SystemReset();
      break;
#endif
/* USER CODE END P2PS_STM_App_Notification_P2P_Evt_Opcode */

    case P2PS_STM_NOTIFY_SHOT_ENABLED_EVT:
/* USER CODE BEGIN P2PS_STM__NOTIFY_ENABLED_EVT */
      P2P_Server_App_Context.ShotNotification_Status = 1;
      APP_DBG_MSG("-- P2P APPLICATION SERVER : NOTIFICATION SHOT ENABLED\r\n"); 
      APP_DBG_MSG(" \n\r");
/* USER CODE END P2PS_STM__NOTIFY_ENABLED_EVT */
      break;

    case P2PS_STM_NOTIFY_SHOT_DISABLED_EVT:
/* USER CODE BEGIN P2PS_STM_NOTIFY_DISABLED_EVT */
      P2P_Server_App_Context.ShotNotification_Status = 0;
      APP_DBG_MSG("-- P2P APPLICATION SERVER : NOTIFICATION SHOT DISABLED\r\n");
      APP_DBG_MSG(" \n\r");
/* USER CODE END P2PS_STM_NOTIFY_DISABLED_EVT */
      break;

    case P2PS_STM_WRITE_EVT:
/* USER CODE BEGIN P2PS_STM_WRITE_EVT */
      if(pNotification->DataTransfered.pPayload[0] == 0x00){ /* Direct connection to UART_TX */
    	  memcpy(Tx_data, &pNotification->DataTransfered.pPayload[1],2);
				APP_DBG_MSG("Send Command %02x %02x %02x\r\n",
                    pNotification->DataTransfered.pPayload[0],
                    pNotification->DataTransfered.pPayload[1],
                    pNotification->DataTransfered.pPayload[2]);
        HAL_UART_Transmit(&hlpuart1,Tx_data,1,1000);
	  }
			if(pNotification->DataTransfered.pPayload[0] == 0x01){ /* Led Test */
    	  if(pNotification->DataTransfered.pPayload[1] == 0x01){
//					HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_SET);
					APP_DBG_MSG("Led blue set\n");
				}
			if(pNotification->DataTransfered.pPayload[1] == 0x00){
//					HAL_GPIO_WritePin(LD1_GPIO_Port, LD1_Pin, GPIO_PIN_RESET);
				APP_DBG_MSG("Led blue reset\n");
				}
	  }
			/* ----------- NOUVEAU : Set SSID via BLE (opcode 0x07) ---------- */
		if(pNotification->DataTransfered.pPayload[0] == 0x07){ /* Set SSID name */
			uint8_t len = pNotification->DataTransfered.Length;  // longueur totale du payload

			if(len > 1)
			{
				char newSsid[16 + 1];  // 16 caractères max + '\0'
				uint8_t nameLen = (uint8_t)(len - 1); // on enlève l’octet d’opcode

				if(nameLen > 16)
					nameLen = 16;

				memset(newSsid, 0, sizeof(newSsid));
				memcpy(newSsid,
							 &pNotification->DataTransfered.pPayload[1],
							 nameLen);

				SetSSIDName(newSsid);  // écrit en EEPROM + met à jour le CRC de la zone NAME

				APP_DBG_MSG("Set SSID via BLE: %s\r\n", newSsid);
			}
		}
			if(pNotification->DataTransfered.pPayload[0] == 0x03){ /* Set timer */
					SetResetTimerValFinish(pNotification->DataTransfered.pPayload[1]);
					APP_DBG_MSG("Set reset timer Finish %02x %02x\r\n",
                    pNotification->DataTransfered.pPayload[0],
                    pNotification->DataTransfered.pPayload[1]);
			}
			if(pNotification->DataTransfered.pPayload[0] == 0x04){ /* Set timer */
					SetResetTimerValNotFinish(pNotification->DataTransfered.pPayload[1]);
					APP_DBG_MSG("Set reset timer Not Finish %02x %02x\r\n",
                    pNotification->DataTransfered.pPayload[0],
                    pNotification->DataTransfered.pPayload[1]);
			}
			if(pNotification->DataTransfered.pPayload[0] == 0x05){ /* SetMode SOLEMS /PSD */
					SetMode(pNotification->DataTransfered.pPayload[1]);
					APP_DBG_MSG("Set Mode\r\n");
			}
			if(pNotification->DataTransfered.pPayload[0] == 0x06){ //Read
				//GetTabShot();
				if (pNotification->DataTransfered.pPayload[1] == 0x03){
					RetValToRead = GetResetTimerValFinish();
					P2PS_STM_App_Update_Char(P2P_WRITE_CHAR_UUID,&RetValToRead,1);
					APP_DBG_MSG("Read reset timer Finish %02x \r\n", RetValToRead);
				}
				if (pNotification->DataTransfered.pPayload[1] == 0x04){
					RetValToRead = GetResetTimerValNotFinish();
					P2PS_STM_App_Update_Char(P2P_WRITE_CHAR_UUID,&RetValToRead,1);
					APP_DBG_MSG("Read reset timer Not Finish %02x \r\n", RetValToRead);
				} 
				if (pNotification->DataTransfered.pPayload[1] == 0x05){
					RetValToRead = GetMode();
					P2PS_STM_App_Update_Char(P2P_WRITE_CHAR_UUID,&RetValToRead,1);
					APP_DBG_MSG("Read Mode value %02x \r\n", RetValToRead);
				}
				if (pNotification->DataTransfered.pPayload[1] == 0x07){
					const char *ssid = GetSSIDName();
					uint8_t buf[16];
					uint8_t ssidLen = (uint8_t)strlen(ssid);
					if(ssidLen > 16)
						ssidLen = 16;

					memcpy(buf, ssid, ssidLen);

					P2PS_STM_App_Update_Char(P2P_WRITE_CHAR_UUID, buf, ssidLen);
					APP_DBG_MSG("Read SSID value: %s\r\n", ssid);
				}
			}
/* USER CODE END P2PS_STM_WRITE_EVT */
      break;
		case P2PS_STM_NOTIFY_RAW_ENABLED_EVT:
			P2P_Server_App_Context.RawNotification_Status = 1;
      APP_DBG_MSG("-- P2P APPLICATION SERVER : NOTIFICATION RAW ENABLED\r\n"); 
      APP_DBG_MSG(" \n\r");
			break;
		case P2PS_STM_NOTIFY_RAW_DISABLED_EVT:
			P2P_Server_App_Context.RawNotification_Status = 0;
      APP_DBG_MSG("-- P2P APPLICATION SERVER : NOTIFICATION RAW DISABLED\r\n");
      APP_DBG_MSG(" \n\r");
			break;
		case P2PS_STM_NOTIFY_TARGET_ENABLED_EVT:
			P2P_Server_App_Context.TargetNotification_Status = 1;
      APP_DBG_MSG("-- P2P APPLICATION SERVER : NOTIFICATION TARGET ENABLED\r\n"); 
      APP_DBG_MSG(" \n\r");
			break;
		case P2PS_STM_NOTIFY_TARGET_DISABLED_EVT:
			P2P_Server_App_Context.TargetNotification_Status = 0;
      APP_DBG_MSG("-- P2P APPLICATION SERVER : NOTIFICATION TARGET DISABLED\r\n");
      APP_DBG_MSG(" \n\r");
			break;
    default:
/* USER CODE BEGIN P2PS_STM_App_Notification_default */

/* USER CODE END P2PS_STM_App_Notification_default */
      break;
  }
/* USER CODE BEGIN P2PS_STM_App_Notification_2 */

/* USER CODE END P2PS_STM_App_Notification_2 */
  return;
}

void P2PS_APP_Notification(P2PS_APP_ConnHandle_Not_evt_t *pNotification)
{
/* USER CODE BEGIN P2PS_APP_Notification_1 */

/* USER CODE END P2PS_APP_Notification_1 */
  switch(pNotification->P2P_Evt_Opcode)
  {
/* USER CODE BEGIN P2PS_APP_Notification_P2P_Evt_Opcode */

/* USER CODE END P2PS_APP_Notification_P2P_Evt_Opcode */
  case PEER_CONN_HANDLE_EVT :
/* USER CODE BEGIN PEER_CONN_HANDLE_EVT */

/* USER CODE END PEER_CONN_HANDLE_EVT */
    break;

    case PEER_DISCON_HANDLE_EVT :
/* USER CODE BEGIN PEER_DISCON_HANDLE_EVT */

/* USER CODE END PEER_DISCON_HANDLE_EVT */
    break;

    default:
/* USER CODE BEGIN P2PS_APP_Notification_default */

/* USER CODE END P2PS_APP_Notification_default */
      break;
  }
/* USER CODE BEGIN P2PS_APP_Notification_2 */

/* USER CODE END P2PS_APP_Notification_2 */
  return;
}

void P2PS_APP_Init(void)
{
/* USER CODE BEGIN P2PS_APP_Init */
  UTIL_SEQ_RegTask( 1<< CFG_TASK_RAW_NOTIFICATION_ID, UTIL_SEQ_RFU, P2PS_Send_Raw_Notification );
  UTIL_SEQ_RegTask( 1<< CFG_TASK_SHOT_NOTIFICATION_ID, UTIL_SEQ_RFU, P2PS_Send_Shot_Notification );
  UTIL_SEQ_RegTask( 1<< CFG_TASK_TARGET_NOTIFICATION_ID, UTIL_SEQ_RFU, P2PS_Send_Target_Notification );

  P2P_Server_App_Context.ShotNotification_Status=0; 
	P2P_Server_App_Context.RawNotification_Status=0;
/* USER CODE END P2PS_APP_Init */
  return;
}

/* USER CODE BEGIN FD */
void storeRawData(uint8_t *data, int lenght )
{
	memcpy(P2P_Server_App_Context.RawTargetToNotification,data,lenght);
}
void storeShotData(uint8_t *data, int lenght )
{
	memcpy(P2P_Server_App_Context.ShotToNotification,data,lenght);
}
void storeTargetData(uint8_t *data, int lenght )
{
	memcpy(P2P_Server_App_Context.TargetToNotification,data,lenght);
}
void P2PS_APP_RAW_Action(void)
{
  UTIL_SEQ_SetTask( 1<<CFG_TASK_RAW_NOTIFICATION_ID, CFG_SCH_PRIO_4);
  return;
}
void P2PS_APP_SHOT_Action(void)
{
  UTIL_SEQ_SetTask( 1<<CFG_TASK_SHOT_NOTIFICATION_ID, CFG_SCH_PRIO_1);
  return;
}
void P2PS_APP_TARGET_Action(void)
{
  UTIL_SEQ_SetTask( 1<<CFG_TASK_TARGET_NOTIFICATION_ID, CFG_SCH_PRIO_2);
  return;
}
/* USER CODE END FD */

/*************************************************************
 *
 * LOCAL FUNCTIONS
 *
 *************************************************************/
/* USER CODE BEGIN FD_LOCAL_FUNCTIONS*/

void P2PS_Send_Raw_Notification(void)
{
   if(P2P_Server_App_Context.RawNotification_Status){
    //APP_DBG_MSG("-- P2P APPLICATION SERVER  : INFORM CLIENT BUTTON 1 PUSHED \r\n ");
    //APP_DBG_MSG(" \n\r");
    P2PS_STM_App_Update_Char(P2P_NOTIFY_RAW_CHAR_UUID, (uint8_t *)&P2P_Server_App_Context.RawTargetToNotification, NOTIFY_RAW_SIZE);
   } else {
    APP_DBG_MSG("-- P2P APPLICATION SERVER : CAN'T INFORM CLIENT -  NOTIFICATION DISABLED\r\n ");
   }
  return;
}
void P2PS_Send_Shot_Notification(void)
{
   if(P2P_Server_App_Context.ShotNotification_Status){
    //APP_DBG_MSG("-- P2P APPLICATION SERVER  : INFORM CLIENT BUTTON 1 PUSHED \r\n ");
    //APP_DBG_MSG(" \n\r");
    P2PS_STM_App_Update_Char(P2P_NOTIFY_SHOT_CHAR_UUID, (uint8_t *)&P2P_Server_App_Context.ShotToNotification, NOTIFY_SHOT_SIZE);
   } else {
    APP_DBG_MSG("-- P2P APPLICATION SERVER : CAN'T INFORM CLIENT -  NOTIFICATION DISABLED\r\n ");
   }
  return;
}
void P2PS_Send_Target_Notification(void)
{
  if(P2P_Server_App_Context.TargetNotification_Status){
    //APP_DBG_MSG("-- P2P APPLICATION SERVER  : INFORM CLIENT BUTTON 1 PUSHED \r\n ");
    //APP_DBG_MSG(" \n\r");
    P2PS_STM_App_Update_Char(P2P_NOTIFY_TARGET_CHAR_UUID, (uint8_t *)&P2P_Server_App_Context.TargetToNotification, NOTIFY_TARGET_SIZE);
   } else {
    APP_DBG_MSG("-- P2P APPLICATION SERVER : CAN'T INFORM CLIENT -  NOTIFICATION DISABLED\r\n ");
   }
  return;
}
/* USER CODE END FD_LOCAL_FUNCTIONS*/
