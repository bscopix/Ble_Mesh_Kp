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
#include "ble_mesh.h"
#include "appli_mesh.h"

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

#define P2P_CTRL_OPCODE_MESH_DEPROVISION            0x08U
#define P2P_CTRL_OPCODE_GET_PROVISIONING_STATUS     0x09U
#define P2P_CTRL_OPCODE_ENTER_PROVISIONING          0x0AU
#define P2P_CTRL_OPCODE_CANCEL_PROVISIONING         0x0BU
#define P2P_CTRL_OPCODE_MESH_HARD_FACTORY_RESET     0x0CU
#define P2P_CTRL_OPCODE_SOFT_REBOOT                 0x0DU

#define P2P_CTRL_STATUS_OK                          0x00U
#define P2P_CTRL_STATUS_BAD_LENGTH                  0x01U
#define P2P_CTRL_STATUS_BAD_STATE                   0x02U
#define P2P_CTRL_STATUS_BUSY                        0x03U
#define P2P_CTRL_STATUS_INTERNAL_ERROR              0x04U

#define P2P_CTRL_MAX_WRITE_LEN                      20U

/* USER CODE END PD */

/* Private macros -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
static uint8_t P2P_CtrlWritePending;
static uint8_t P2P_CtrlWriteLen;
static uint8_t P2P_CtrlWritePayload[P2P_CTRL_MAX_WRITE_LEN];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

static void P2PS_Send_Raw_Notification(void);
static void P2PS_Send_Shot_Notification(void);
static void P2PS_Send_Target_Notification(void);
static void P2PS_Send_Ctl_Response(uint8_t request_opcode, uint8_t status, const uint8_t *payload, uint8_t payload_len);
static void P2PS_Build_Provisioning_Status(uint8_t *status_payload4);
static void P2PS_Handle_Mesh_Control_Write(const uint8_t *payload, uint8_t length);
static void P2PS_Process_Pending_Ctrl_Write(void);
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
      if (pNotification->DataTransfered.Length > 0U)
      {
        uint8_t opcode = pNotification->DataTransfered.pPayload[0];

        if (opcode == P2P_CTRL_OPCODE_SOFT_REBOOT)
        {
          /* On attend 1 octet (opcode seul) */
          if (pNotification->DataTransfered.Length != 1U)
          {
            P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_BAD_LENGTH, NULL, 0U);
            break;
          }
          P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_OK, NULL, 0U);
          APP_DBG_MSG("SOFT_REBOOT accepted\r\n");
          HAL_Delay(100U);
          NVIC_SystemReset();
        }

        if ((opcode == P2P_CTRL_OPCODE_MESH_DEPROVISION)
          || (opcode == P2P_CTRL_OPCODE_GET_PROVISIONING_STATUS)
            || (opcode == P2P_CTRL_OPCODE_ENTER_PROVISIONING)
            || (opcode == P2P_CTRL_OPCODE_CANCEL_PROVISIONING)
            || (opcode == P2P_CTRL_OPCODE_MESH_HARD_FACTORY_RESET))
        {
          if (pNotification->DataTransfered.Length <= P2P_CTRL_MAX_WRITE_LEN)
          {
            /* ENTER_PROVISIONING must be handled immediately to avoid
             * scheduler latency preventing the expected reboot path. */
            if (opcode == P2P_CTRL_OPCODE_ENTER_PROVISIONING)
            {
              APP_DBG_MSG("P2P ctrl immediate: op=0x%02x len=%u\r\n",
                          opcode,
                          pNotification->DataTransfered.Length);
              P2PS_Handle_Mesh_Control_Write(pNotification->DataTransfered.pPayload,
                                             pNotification->DataTransfered.Length);
            }
            else
            {
              memcpy(P2P_CtrlWritePayload,
                     pNotification->DataTransfered.pPayload,
                     pNotification->DataTransfered.Length);
              P2P_CtrlWriteLen = pNotification->DataTransfered.Length;
              P2P_CtrlWritePending = 1U;
              APP_DBG_MSG("P2P ctrl queued: op=0x%02x len=%u\r\n",
                          opcode,
                          pNotification->DataTransfered.Length);
              UTIL_SEQ_SetTask(1 << CFG_TASK_P2P_CTRL_REQ_ID, CFG_SCH_PRIO_0);
            }
          }
          else
          {
            APP_DBG_MSG("P2P ctrl write dropped: length=%u > %u\r\n",
                        pNotification->DataTransfered.Length,
                        (unsigned int)P2P_CTRL_MAX_WRITE_LEN);
          }
          break;
        }

        if ((IsProvisioningRuntimeSessionActive() == true) ||
            (GetProvisioningResult() == PROVISION_RESULT_SUCCESS))
        {
          APP_DBG_MSG("Legacy P2P opcode ignored while Mesh/Proxy active: op=0x%02x len=%u\r\n",
                      opcode,
                      pNotification->DataTransfered.Length);
          break;
        }
      }

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
				char newSsid[16 + 1];  // 16 caract�res max + '\0'
				uint8_t nameLen = (uint8_t)(len - 1); // on enl�ve l�octet d�opcode

				if(nameLen > 16)
					nameLen = 16;

				memset(newSsid, 0, sizeof(newSsid));
				memcpy(newSsid,
							 &pNotification->DataTransfered.pPayload[1],
							 nameLen);

				SetSSIDName(newSsid);  // �crit en EEPROM + met � jour le CRC de la zone NAME

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
  UTIL_SEQ_RegTask( 1<< CFG_TASK_P2P_CTRL_REQ_ID, UTIL_SEQ_RFU, P2PS_Process_Pending_Ctrl_Write );

  P2P_Server_App_Context.ShotNotification_Status=0; 
	P2P_Server_App_Context.RawNotification_Status=0;
  P2P_CtrlWritePending = 0U;
  P2P_CtrlWriteLen = 0U;
/* USER CODE END P2PS_APP_Init */
  return;
}

/* USER CODE BEGIN FD */
void storeRawData(uint8_t *data, int lenght )
{
  if ((data == NULL) || (lenght <= 0))
  {
    return;
  }

  if ((uint32_t)lenght > (uint32_t)NOTIFY_RAW_SIZE)
  {
    lenght = NOTIFY_RAW_SIZE;
  }

	memcpy(P2P_Server_App_Context.RawTargetToNotification, data, (size_t)lenght);
}
void storeShotData(uint8_t *data, int lenght )
{
  if ((data == NULL) || (lenght <= 0))
  {
    return;
  }

  if ((uint32_t)lenght > (uint32_t)NOTIFY_SHOT_SIZE)
  {
    lenght = NOTIFY_SHOT_SIZE;
  }

	memcpy(P2P_Server_App_Context.ShotToNotification, data, (size_t)lenght);
}
void storeTargetData(uint8_t *data, int lenght )
{
  if ((data == NULL) || (lenght <= 0))
  {
    return;
  }

  if ((uint32_t)lenght > (uint32_t)NOTIFY_TARGET_SIZE)
  {
    lenght = NOTIFY_TARGET_SIZE;
  }

	memcpy(P2P_Server_App_Context.TargetToNotification, data, (size_t)lenght);
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

static void P2PS_Send_Ctl_Response(uint8_t request_opcode, uint8_t status, const uint8_t *payload, uint8_t payload_len)
{
  uint8_t response[7U];
  uint8_t tx_len = (uint8_t)(3U + payload_len);

  response[0] = (uint8_t)(request_opcode | 0x80U);
  response[1] = status;
  response[2] = payload_len;

  if ((payload_len > 0U) && (payload != NULL))
  {
    memcpy(&response[3], payload, payload_len);
  }

  P2PS_STM_App_Update_Char(P2P_WRITE_CHAR_UUID, response, tx_len);
}

static void P2PS_Build_Provisioning_Status(uint8_t *status_payload4)
{
  uint16_t unicast;
  uint8_t is_unprovisioned;
  uint8_t runtime_active;
  uint8_t provision_result;

  runtime_active = (uint8_t)(IsProvisioningRuntimeSessionActive() ? 1U : 0U);
  is_unprovisioned = (uint8_t)((BLEMesh_IsUnprovisioned() == MOBLE_TRUE) ? 1U : 0U);

  status_payload4[0] = runtime_active;
  status_payload4[1] = (is_unprovisioned != 0U) ? 0x00U : 0x01U;

  /* In legacy GATT boot mode Mesh may be inactive; use persisted result fallback. */
  provision_result = GetProvisioningResult();
  if ((status_payload4[0] == 0x00U) && (provision_result == PROVISION_RESULT_SUCCESS))
  {
    status_payload4[1] = 0x01U;
  }

  unicast = (uint16_t)BLEMesh_GetAddress();
  if (status_payload4[1] == 0x00U)
  {
    unicast = 0U;
  }

  status_payload4[2] = (uint8_t)(unicast & 0xFFU);
  status_payload4[3] = (uint8_t)((unicast >> 8) & 0xFFU);
}

static void P2PS_Process_Pending_Ctrl_Write(void)
{
  uint8_t payload_len;
  uint8_t payload_copy[P2P_CTRL_MAX_WRITE_LEN];

  if (P2P_CtrlWritePending == 0U)
  {
    return;
  }

  payload_len = P2P_CtrlWriteLen;
  if ((payload_len == 0U) || (payload_len > P2P_CTRL_MAX_WRITE_LEN))
  {
    P2P_CtrlWritePending = 0U;
    P2P_CtrlWriteLen = 0U;
    return;
  }

  memcpy(payload_copy, P2P_CtrlWritePayload, payload_len);
  P2P_CtrlWritePending = 0U;
  P2P_CtrlWriteLen = 0U;

  P2PS_Handle_Mesh_Control_Write(payload_copy, payload_len);
}

static void P2PS_Handle_Mesh_Control_Write(const uint8_t *payload, uint8_t length)
{
  uint8_t opcode;
  uint8_t declared_payload_len;
  uint8_t actual_payload_len;
  uint8_t status_payload[4U];
  uint16_t timeout_s;
  MOBLE_RESULT mesh_result;

  if ((payload == NULL) || (length == 0U))
  {
    return;
  }

  opcode = payload[0];

  if (length < 2U)
  {
    P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_BAD_LENGTH, NULL, 0U);
    return;
  }

  declared_payload_len = payload[1];
  actual_payload_len = (uint8_t)(length - 2U);

  if (declared_payload_len != actual_payload_len)
  {
    P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_BAD_LENGTH, NULL, 0U);
    return;
  }

  switch (opcode)
  {
    case P2P_CTRL_OPCODE_MESH_DEPROVISION:
      if (declared_payload_len != 0U)
      {
        P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_BAD_LENGTH, NULL, 0U);
        break;
      }

      if (BLEMesh_IsUnprovisioned() == MOBLE_TRUE)
      {
        P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_BAD_STATE, NULL, 0U);
        APP_DBG_MSG("MESH_DEPROVISION rejected: node already unprovisioned\r\n");
        break;
      }

      if (BLEMesh_TrsptIsBusyState() == MOBLE_TRUE)
      {
        P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_BUSY, NULL, 0U);
        APP_DBG_MSG("MESH_DEPROVISION rejected: mesh transport busy\r\n");
        break;
      }

      mesh_result = BLEMesh_Unprovision();
      if (MOBLE_FAILED(mesh_result))
      {
        P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_INTERNAL_ERROR, NULL, 0U);
        APP_DBG_MSG("MESH_DEPROVISION failed: result=%d\r\n", mesh_result);
        break;
      }

      ClearProvisioningBootRequest(PROVISION_RESULT_UNKNOWN);
      P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_OK, NULL, 0U);
      APP_DBG_MSG("MESH_DEPROVISION accepted: rebooting node\r\n");
      HAL_Delay(100U);
      NVIC_SystemReset();
      break;

    case P2P_CTRL_OPCODE_GET_PROVISIONING_STATUS:
      P2PS_Build_Provisioning_Status(status_payload);
      P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_OK, status_payload, sizeof(status_payload));
      APP_DBG_MSG("Provisioning status requested: mode=%02x prov=%02x addr=%02x%02x\r\n",
                  status_payload[0], status_payload[1], status_payload[3], status_payload[2]);
      break;

    case P2P_CTRL_OPCODE_ENTER_PROVISIONING:
      if (declared_payload_len != 2U)
      {
        P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_BAD_LENGTH, NULL, 0U);
        break;
      }

      timeout_s = (uint16_t)payload[2] | ((uint16_t)payload[3] << 8);
      SetProvisioningBootRequest(timeout_s, PROVISION_REASON_GATT);
      P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_OK, NULL, 0U);
      APP_DBG_MSG("Provisioning boot requested via GATT (timeout=%u s)\r\n", timeout_s);
      HAL_Delay(100U);
      NVIC_SystemReset();
      break;

    case P2P_CTRL_OPCODE_CANCEL_PROVISIONING:
      if (declared_payload_len != 0U)
      {
        P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_BAD_LENGTH, NULL, 0U);
        break;
      }

      ClearProvisioningBootRequest(PROVISION_RESULT_UNKNOWN);
      P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_OK, NULL, 0U);
      APP_DBG_MSG("Provisioning boot request canceled via GATT\r\n");
      break;

    case P2P_CTRL_OPCODE_MESH_HARD_FACTORY_RESET:
      if (declared_payload_len != 0U)
      {
        P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_BAD_LENGTH, NULL, 0U);
        break;
      }

      ClearProvisioningBootRequest(PROVISION_RESULT_UNKNOWN);
      ClearNfcBootOpcode();

      mesh_result = Appli_MeshEraseProvisioningStorage();
      if (MOBLE_FAILED(mesh_result))
      {
        P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_INTERNAL_ERROR, NULL, 0U);
        APP_DBG_MSG("MESH_HARD_FACTORY_RESET failed: erase result=%d\r\n", mesh_result);
        break;
      }

      P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_OK, NULL, 0U);
      APP_DBG_MSG("MESH_HARD_FACTORY_RESET accepted: rebooting node\r\n");
      HAL_Delay(100U);
      NVIC_SystemReset();
      break;

    default:
      P2PS_Send_Ctl_Response(opcode, P2P_CTRL_STATUS_INTERNAL_ERROR, NULL, 0U);
      break;
  }
}
/* USER CODE END FD_LOCAL_FUNCTIONS*/
