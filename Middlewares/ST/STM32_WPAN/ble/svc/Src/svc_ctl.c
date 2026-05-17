/**
 ******************************************************************************
 * @file    svc_ctl.c
 * @author  MCD Application Team
 * @brief   BLE Controller
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2018-2021 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */


/* Includes ------------------------------------------------------------------*/
#include "common_blesvc.h"
#include "cmsis_compiler.h"
#include "mesh_cfg_usr.h"
#include "nfc_eeprom_mngt.h"
#include "app_debug.h"

/* Private typedef -----------------------------------------------------------*/
typedef struct
{
#if (BLE_CFG_SVC_MAX_NBR_CB > 0)
SVC_CTL_p_EvtHandler_t SVCCTL__SvcHandlerTab[BLE_CFG_SVC_MAX_NBR_CB];
#endif
uint8_t NbreOfRegisteredHandler;
} SVCCTL_EvtHandler_t;

typedef struct
{
#if (BLE_CFG_CLT_MAX_NBR_CB > 0)
SVC_CTL_p_EvtHandler_t SVCCTL_CltHandlerTable[BLE_CFG_CLT_MAX_NBR_CB];
#endif
uint8_t NbreOfRegisteredHandler;
} SVCCTL_CltHandler_t;

/* Private defines -----------------------------------------------------------*/
#define SVCCTL_EGID_EVT_MASK   0xFF00
#define SVCCTL_GATT_EVT_TYPE   0x0C00
#define SVCCTL_GAP_DEVICE_NAME_LENGTH 7

/* Private macros ------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/**
 * START of Section BLE_DRIVER_CONTEXT
 */

PLACE_IN_SECTION("BLE_DRIVER_CONTEXT") SVCCTL_EvtHandler_t SVCCTL_EvtHandler;
PLACE_IN_SECTION("BLE_DRIVER_CONTEXT") SVCCTL_CltHandler_t SVCCTL_CltHandler;

/**
 * END of Section BLE_DRIVER_CONTEXT
 */

/* Private functions ----------------------------------------------------------*/

/* Weak functions ----------------------------------------------------------*/
void BVOPUS_STM_Init(void);

__WEAK void BAS_Init( void )
{
  return;
}

__WEAK void BLS_Init( void )
{
  return;
}
__WEAK void CRS_STM_Init( void )
{
  return;
}
__WEAK void DIS_Init( void )
{
  return;
}
__WEAK void EDS_STM_Init( void )
{
  return;
}
__WEAK void HIDS_Init( void )
{
  return;
}
__WEAK void HRS_Init( void )
{
  return;
}
__WEAK void HTS_Init( void )
{
  return;
}
__WEAK void IAS_Init( void )
{
  return;
}
__WEAK void LLS_Init( void )
{
  return;
}
__WEAK void TPS_Init( void )
{
  return;
}
__WEAK void MOTENV_STM_Init( void )
{
  return;
}
__WEAK void P2PS_STM_Init( void )
{
  return;
}
__WEAK void ZDD_STM_Init( void )
{
  return;
}
__WEAK void OTAS_STM_Init( void )
{
  return;
}
__WEAK void MESH_Init( void )
{
  return;
}
__WEAK void BVOPUS_STM_Init( void )
{
  return;
}
__WEAK void SVCCTL_InitCustomSvc( void )
{
  return;
}

/* Functions Definition ------------------------------------------------------*/

void SVCCTL_Init( void )
{
 
  /**
   * Initialize the number of registered Handler
   */
  SVCCTL_EvtHandler.NbreOfRegisteredHandler = 0;
  SVCCTL_CltHandler.NbreOfRegisteredHandler = 0;

  /**
   * Add and Initialize requested services
   */
  SVCCTL_SvcInit();

  return;
}

__WEAK void SVCCTL_SvcInit(void)
{
  const uint8_t provisioning_boot_requested = (uint8_t)IsProvisioningBootRequested();
  const uint8_t provisioning_success_persisted =
      (uint8_t)((GetProvisioningResult() == PROVISION_RESULT_SUCCESS) ? 1U : 0U);

  BAS_Init();

  BLS_Init();

  CRS_STM_Init();

  DIS_Init();

  EDS_STM_Init();

  HIDS_Init();

  HRS_Init();

  HTS_Init();

  IAS_Init();

  LLS_Init();

  TPS_Init();

  MOTENV_STM_Init();

  P2PS_STM_Init();

  ZDD_STM_Init();

  OTAS_STM_Init();

  BVOPUS_STM_Init();

  SVCCTL_InitCustomSvc();

  if (provisioning_boot_requested != 0U)
  {
    APP_DBG_MSG("[SVCCTL] Provisioning boot: full service initialization\r\n");
  }
  else if (provisioning_success_persisted != 0U)
  {
    APP_DBG_MSG("[SVCCTL] Provisioned boot: enabling Mesh runtime for Proxy availability\r\n");
  }
  else
  {
    APP_DBG_MSG("[SVCCTL] Unprovisioned/unknown boot: legacy GATT advertising path\r\n");
  }

#ifndef DISABLE_MESH_AUTOSTART
  MESH_Init();
#else
  if (provisioning_boot_requested != 0U)
  {
    StartProvisioningRuntimeSession(GetProvisioningTimeoutSeconds());
    /* Consume one-shot boot request to avoid permanent Mesh autostart on next reboot. */
    ClearProvisioningBootRequest(PROVISION_RESULT_UNKNOWN);
    MESH_Init();
  }
  else if (provisioning_success_persisted != 0U)
  {
    /* Node already provisioned: start Mesh at normal reboot so GATT Proxy stays reachable. */
    MESH_Init();
  }
  else
  {
    /* Ensure legacy GATT startup is not blocked by a stale runtime session flag. */
    StopProvisioningRuntimeSession(PROVISION_RESULT_UNKNOWN);
  }
#endif

  APP_DBG_MSG("[SVCCTL] SvcInit complete: handlers=%u max=%u\r\n",
              SVCCTL_EvtHandler.NbreOfRegisteredHandler,
              BLE_CFG_SVC_MAX_NBR_CB);
  
  return;
}

/**
 * @brief  BLE Controller initialization
 * @param  None
 * @retval None
 */
void SVCCTL_RegisterSvcHandler( SVC_CTL_p_EvtHandler_t pfBLE_SVC_Service_Event_Handler )
{
#if (BLE_CFG_SVC_MAX_NBR_CB > 0)
  if (SVCCTL_EvtHandler.NbreOfRegisteredHandler < BLE_CFG_SVC_MAX_NBR_CB)
  {
    SVCCTL_EvtHandler.SVCCTL__SvcHandlerTab[SVCCTL_EvtHandler.NbreOfRegisteredHandler] = pfBLE_SVC_Service_Event_Handler;
    SVCCTL_EvtHandler.NbreOfRegisteredHandler++;
    APP_DBG_MSG("[SVCCTL] RegisterSvcHandler idx=%u/%u ptr=0x%08x\r\n",
                SVCCTL_EvtHandler.NbreOfRegisteredHandler,
                BLE_CFG_SVC_MAX_NBR_CB,
                (unsigned int)pfBLE_SVC_Service_Event_Handler);
  }
  else
  {
    APP_DBG_MSG("[SVCCTL] RegisterSvcHandler overflow max=%u ptr=0x%08x\r\n",
                BLE_CFG_SVC_MAX_NBR_CB,
                (unsigned int)pfBLE_SVC_Service_Event_Handler);
  }
#else
  (void)(pfBLE_SVC_Service_Event_Handler);
#endif

  return;
}

/**
 * @brief  BLE Controller initialization
 * @param  None
 * @retval None
 */
void SVCCTL_RegisterCltHandler( SVC_CTL_p_EvtHandler_t pfBLE_SVC_Client_Event_Handler )
{
#if (BLE_CFG_CLT_MAX_NBR_CB > 0)
  SVCCTL_CltHandler.SVCCTL_CltHandlerTable[SVCCTL_CltHandler.NbreOfRegisteredHandler] = pfBLE_SVC_Client_Event_Handler;
  SVCCTL_CltHandler.NbreOfRegisteredHandler++;
#else
  (void)(pfBLE_SVC_Client_Event_Handler);
#endif

  return;
}

__WEAK SVCCTL_UserEvtFlowStatus_t SVCCTL_UserEvtRx( void *pckt )
{
  hci_event_pckt *event_pckt;
  evt_blecore_aci *blecore_evt = NULL;
  SVCCTL_EvtAckStatus_t event_notification_status;
  SVCCTL_UserEvtFlowStatus_t return_status;
  uint8_t index;

  event_pckt = (hci_event_pckt*) ((hci_uart_pckt *) pckt)->data;
  event_notification_status = SVCCTL_EvtNotAck;

  switch (event_pckt->evt)
  {
    case HCI_DISCONNECTION_COMPLETE_EVT_CODE:
    {
      hci_disconnection_complete_event_rp0 *p_disc_evt =
          (hci_disconnection_complete_event_rp0 *)event_pckt->data;

      APP_DBG_MSG("[SVCCTL][DISC] handle=0x%04x reason=0x%02x status=0x%02x\r\n",
                  p_disc_evt->Connection_Handle,
                  p_disc_evt->Reason,
                  p_disc_evt->Status);
      break;
    }

    case HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE:
    {
      blecore_evt = (evt_blecore_aci*) event_pckt->data;

      if (((blecore_evt->ecode & SVCCTL_EGID_EVT_MASK) == SVCCTL_GATT_EVT_TYPE) &&
          (blecore_evt->ecode != 0x0C01U) &&
          (blecore_evt->ecode != ACI_HAL_END_OF_RADIO_ACTIVITY_VSEVT_CODE))
      {
        switch (blecore_evt->ecode)
        {
          case ACI_GATT_PROC_TIMEOUT_VSEVT_CODE:
          {
            aci_gatt_proc_timeout_event_rp0 *p_to = (aci_gatt_proc_timeout_event_rp0 *)blecore_evt->data;
            APP_DBG_MSG("[SVCCTL][GATT_VSEVT][PROC_TIMEOUT] conn=0x%04x\r\n",
                        p_to->Connection_Handle);
            break;
          }

          case ACI_GATT_ERROR_RESP_VSEVT_CODE:
          {
            aci_gatt_error_resp_event_rp0 *p_err = (aci_gatt_error_resp_event_rp0 *)blecore_evt->data;
            APP_DBG_MSG("[SVCCTL][GATT_VSEVT][ERROR_RESP] conn=0x%04x req=0x%02x attr=0x%04x err=0x%02x\r\n",
                        p_err->Connection_Handle,
                        p_err->Req_Opcode,
                        p_err->Attribute_Handle,
                        p_err->Error_Code);
            break;
          }

          default:
            break;
        }
      }

      switch ((blecore_evt->ecode) & SVCCTL_EGID_EVT_MASK)
      {
        case SVCCTL_GATT_EVT_TYPE:
#if (BLE_CFG_SVC_MAX_NBR_CB > 0)
          /* For Service event handler */
          for (index = 0; index < SVCCTL_EvtHandler.NbreOfRegisteredHandler; index++)
          {
            event_notification_status = SVCCTL_EvtHandler.SVCCTL__SvcHandlerTab[index](pckt);
            /**
             * When a GATT event has been acknowledged by a Service, there is no need to call the other registered handlers
             * a GATT event is relevant for only one Service
             */
            if (event_notification_status != SVCCTL_EvtNotAck)
            {
              /**
               *  The event has been managed. The Event processing should be stopped
               */
              break;
            }
          }
#endif
#if (BLE_CFG_CLT_MAX_NBR_CB > 0)
          /* For Client event handler */
          event_notification_status = SVCCTL_EvtNotAck;
          for(index = 0; index <SVCCTL_CltHandler.NbreOfRegisteredHandler; index++)
          {
            event_notification_status = SVCCTL_CltHandler.SVCCTL_CltHandlerTable[index](pckt);
            /**
             * When a GATT event has been acknowledged by a Client, there is no need to call the other registered handlers
             * a GATT event is relevant for only one Client
             */
            if (event_notification_status != SVCCTL_EvtNotAck)
            {
              /**
               *  The event has been managed. The Event processing should be stopped
               */
              break;
            }
          }
#endif
          break;

        default:
          break;
      }
    }
      break; /* HCI_HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE_SPECIFIC */

    default:
      break;
  }

  /**
   * When no registered handlers (either Service or Client) has acknowledged the GATT event, it is reported to the application
   * a GAP event is always reported to the application.
   */
  switch (event_notification_status)
  {
    case SVCCTL_EvtNotAck:
      /**
       *  The event has NOT been managed.
       *  It shall be passed to the application for processing
       */
      return_status = SVCCTL_App_Notification(pckt);
      break;

    case SVCCTL_EvtAckFlowEnable:
      return_status = SVCCTL_UserEvtFlowEnable;
      break;

    case SVCCTL_EvtAckFlowDisable:
      return_status = SVCCTL_UserEvtFlowDisable;
      break;

    default:
      return_status = SVCCTL_UserEvtFlowEnable;
      break;
  }

  return (return_status);
}


