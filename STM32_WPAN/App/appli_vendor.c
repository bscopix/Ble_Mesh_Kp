/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    appli_vendor.c
  * @author  MCD Application Team
  * @brief   Application interface for Vendor Mesh Models 
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2021 STMicroelectronics.
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
#include "app_conf.h"
#include "hal_common.h"
#include "types.h"
#include "ble_mesh.h"
#include "appli_mesh.h"
#include "vendor.h"
#include "appli_vendor.h"
#include "common.h"
#include "appli_light.h"
#include "models_if.h"
#include "mesh_cfg.h"
#include "ctor10-w_data.h"
#include "p2p_server_app.h"
#include "dis_app.h"
#include "nfc_eeprom_mngt.h"
#include "mode_manager.h"
#include <string.h>
extern UART_HandleTypeDef hlpuart1;

/** @addtogroup ST_BLE_Mesh
*  @{
*/

/** @addtogroup Application_Mesh_Models
*  @{
*/

/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
typedef struct
{
  MOBLE_ADDRESS dst;
  MOBLEUINT8 data[VENDOR_DATA_BUFFER_SIZE]; 
  MOBLEUINT32 length;
  MOBLEUINT8 elementIndex;
} APPLI_SEND_BIG_DATA_PACKET;

typedef struct
{
  MOBLEUINT8 pending;
  MOBLEUINT32 sequence;
  MOBLEUINT8 data[NOTIFY_TARGET_SIZE];
} APPLI_VENDOR_TARGET_EVENT;

typedef struct
{
  MOBLEUINT8 pending;
  MOBLEUINT32 sequence;
  MOBLEUINT8 level;
} APPLI_VENDOR_BATTERY_EVENT;

#define APPLI_VENDOR_EVENT_HEADER_SIZE       5U
#define APPLI_VENDOR_RETRY_DELAY_MS        100U
#define APPLI_VENDOR_ERROR_TRACE_DELAY_MS 5000U

/* Private variables ---------------------------------------------------------*/

MOBLEUINT8 ResponseBuffer[VENDOR_DATA_BUFFER_SIZE];
MOBLEUINT16 BuffLength;
APPLI_SEND_BIG_DATA_PACKET Appli_VendorBigData;
static MOBLEUINT8 VendorShotPending;
static MOBLEUINT32 VendorShotNextSequence;
static MOBLEUINT32 VendorShotEndSequence;
static APPLI_VENDOR_TARGET_EVENT VendorTargetEvent;
static APPLI_VENDOR_BATTERY_EVENT VendorBatteryEvent;
static MOBLEUINT32 VendorTargetNextSequence;
static MOBLEUINT32 VendorBatteryNextSequence;
static MOBLEUINT32 VendorNextRetryTick;
static MOBLEUINT32 VendorNextErrorTraceTick;

/*Variable to enable OTA for received vendor command*/
extern MOBLEUINT8 Appli_LedState;
#ifdef USER_BOARD_1LED
extern uint16_t DUTY;
#endif
extern MOBLEUINT8 NumberOfElements;
MOBLEUINT32 TestHitCounter = 0;
extern Appli_LightPwmValue_t Appli_LightPwmValue;

/* Private function prototypes -----------------------------------------------*/
static MOBLEBOOL Appli_Vendor_IsMeshOperational(void);
static MOBLE_RESULT Appli_Vendor_CopyStringResponse(const char *value);
static MOBLE_RESULT Appli_Vendor_SendEvent(MOBLEUINT8 subcmd,
                                           MOBLEUINT32 sequence,
                                           const MOBLEUINT8 *data,
                                           MOBLEUINT8 length);
/* Private functions ---------------------------------------------------------*/

static MOBLEBOOL Appli_Vendor_IsMeshOperational(void)
{
  ModeStatus_t mode_status;

  ModeManager_GetStatus(&mode_status);
  return ((mode_status.actual_mode == (uint8_t)BOOT_MODE_MESH_OPERATIONAL) &&
          (GetProvisioningResult() == PROVISION_RESULT_SUCCESS))
      ? MOBLE_TRUE
      : MOBLE_FALSE;
}

static MOBLE_RESULT Appli_Vendor_CopyStringResponse(const char *value)
{
  size_t value_length;

  if (value == NULL)
  {
    return MOBLE_RESULT_INVALIDARG;
  }

  value_length = strlen(value);
  if (value_length > (sizeof(ResponseBuffer) - 1U))
  {
    value_length = sizeof(ResponseBuffer) - 1U;
  }

  memcpy(&ResponseBuffer[1], value, value_length);
  BuffLength = (MOBLEUINT16)(1U + value_length);
  return MOBLE_RESULT_SUCCESS;
}

static MOBLE_RESULT Appli_Vendor_SendEvent(MOBLEUINT8 subcmd,
                                           MOBLEUINT32 sequence,
                                           const MOBLEUINT8 *data,
                                           MOBLEUINT8 length)
{
  MOBLEUINT8 event_buffer[APPLI_VENDOR_EVENT_HEADER_SIZE + NOTIFY_SHOT_SIZE];

  if ((data == NULL) ||
      ((MOBLEUINT32)length > (sizeof(event_buffer) - APPLI_VENDOR_EVENT_HEADER_SIZE)))
  {
    return MOBLE_RESULT_INVALIDARG;
  }

  event_buffer[0] = subcmd;
  event_buffer[1] = (MOBLEUINT8)(sequence & 0xFFU);
  event_buffer[2] = (MOBLEUINT8)((sequence >> 8) & 0xFFU);
  event_buffer[3] = (MOBLEUINT8)((sequence >> 16) & 0xFFU);
  event_buffer[4] = (MOBLEUINT8)((sequence >> 24) & 0xFFU);
  memcpy(&event_buffer[APPLI_VENDOR_EVENT_HEADER_SIZE], data, length);

  return BLEMesh_SetRemotePublication(VENDORMODEL_STMICRO_ID1,
                                      BLEMesh_GetAddress(),
                                      APPLI_DATA_CNTRL_CMD,
                                      event_buffer,
                                      (MOBLEUINT32)APPLI_VENDOR_EVENT_HEADER_SIZE + length,
                                      MOBLE_FALSE,
                                      MOBLE_TRUE);
}

void Appli_Vendor_QueueShotEvent(const MOBLEUINT8 *data,
                                 MOBLEUINT8 length,
                                 MOBLEUINT32 sequence)
{
  if ((data == NULL) || (length != NOTIFY_SHOT_SIZE) ||
      (Appli_Vendor_IsMeshOperational() == MOBLE_FALSE))
  {
    return;
  }

  if (VendorShotPending == 0U)
  {
    VendorShotNextSequence = sequence;
    VendorShotPending = 1U;
  }

  if (sequence >= VendorShotNextSequence)
  {
    VendorShotEndSequence = sequence + 1U;
  }
}

void Appli_Vendor_QueueTargetEvent(const MOBLEUINT8 *data, MOBLEUINT8 length)
{
  if ((data == NULL) || (length != NOTIFY_TARGET_SIZE) ||
      (Appli_Vendor_IsMeshOperational() == MOBLE_FALSE))
  {
    return;
  }

  VendorTargetEvent.sequence = VendorTargetNextSequence++;
  memcpy(VendorTargetEvent.data, data, NOTIFY_TARGET_SIZE);
  VendorTargetEvent.pending = 1U;
}

void Appli_Vendor_QueueBatteryEvent(MOBLEUINT8 level)
{
  if (Appli_Vendor_IsMeshOperational() == MOBLE_FALSE)
  {
    return;
  }

  VendorBatteryEvent.sequence = VendorBatteryNextSequence++;
  VendorBatteryEvent.level = level;
  VendorBatteryEvent.pending = 1U;
}

void Appli_Vendor_ProcessEvents(void)
{
  MOBLE_RESULT result;
  MOBLEUINT32 now;
  MOBLEUINT8 event_data[NOTIFY_SHOT_SIZE];
  MOBLEUINT32 oldest_sequence;

  if (Appli_Vendor_IsMeshOperational() == MOBLE_FALSE)
  {
    return;
  }

  now = HAL_GetTick();
  if ((int32_t)(now - VendorNextRetryTick) < 0)
  {
    return;
  }

  if (BLEMesh_TrsptIsBusyState())
  {
    return;
  }

  if (VendorShotPending != 0U)
  {
    oldest_sequence = GetShotQueueOldestSequence();
    if (VendorShotNextSequence < oldest_sequence)
    {
      TRACE_I(TF_VENDOR_M,
              "Vendor shot event backlog overrun: dropped=%lu\r\n",
              (unsigned long)(oldest_sequence - VendorShotNextSequence));
      VendorShotNextSequence = oldest_sequence;
    }

    if (VendorShotNextSequence >= VendorShotEndSequence)
    {
      VendorShotPending = 0U;
      return;
    }

    if (!GetShotBySequence(VendorShotNextSequence, event_data, sizeof(event_data)))
    {
      VendorShotPending = 0U;
      return;
    }

    result = Appli_Vendor_SendEvent(APPLI_SHOT_EVENT,
                                    VendorShotNextSequence,
                                    event_data,
                                    NOTIFY_SHOT_SIZE);
    if (result == MOBLE_RESULT_SUCCESS)
    {
      VendorShotNextSequence++;
      if (VendorShotNextSequence >= VendorShotEndSequence)
      {
        VendorShotPending = 0U;
      }
      return;
    }
  }
  else if (VendorTargetEvent.pending != 0U)
  {
    result = Appli_Vendor_SendEvent(APPLI_TARGET_EVENT,
                                    VendorTargetEvent.sequence,
                                    VendorTargetEvent.data,
                                    NOTIFY_TARGET_SIZE);
    if (result == MOBLE_RESULT_SUCCESS)
    {
      VendorTargetEvent.pending = 0U;
      return;
    }
  }
  else if (VendorBatteryEvent.pending != 0U)
  {
    result = Appli_Vendor_SendEvent(APPLI_BATTERY_EVENT,
                                    VendorBatteryEvent.sequence,
                                    &VendorBatteryEvent.level,
                                    1U);
    if (result == MOBLE_RESULT_SUCCESS)
    {
      VendorBatteryEvent.pending = 0U;
      return;
    }
  }
  else
  {
    return;
  }

  VendorNextRetryTick = now + APPLI_VENDOR_RETRY_DELAY_MS;
  if ((int32_t)(now - VendorNextErrorTraceTick) >= 0)
  {
    TRACE_I(TF_VENDOR_M, "Vendor event publication deferred: result=%u\r\n", result);
    VendorNextErrorTraceTick = now + APPLI_VENDOR_ERROR_TRACE_DELAY_MS;
  }
}

/**
* @brief  Process the Vendor Device Info Command
* @param  data: Pointer to the data received from peer_addr
* @param  length: Length of the data
* @retval MOBLE_RESULT status of result
*/          
MOBLE_RESULT Appli_Vendor_DeviceInfo(MOBLEUINT8 const *data, MOBLEUINT32 length)
{
  MOBLEUINT8 tempBuffer[10];
  MOBLE_RESULT status = MOBLE_RESULT_SUCCESS;  
  
  MOBLEUINT8 subCmd = data[0];
  char *libVersion;
  char *subLibVersion;
  MOBLEUINT8 inc = 0;
        /*First Byte is sending the Sub Command*/      
  ResponseBuffer[0] = subCmd;
        TRACE_M(TF_VENDOR,"#02-%02hx! \n\r",data[0]);
  
  switch(subCmd)
  {
  case IC_TYPE:
    {
#ifdef BLUENRG1_DEVICE           
      ResponseBuffer[1] = BLUENRG1_BRD;              
#endif
      
#ifdef BLUENRG2_DEVICE              
      ResponseBuffer[1] = BLUENRG2_BRD;              
#endif
      
#ifdef BLUENRG_MS    
      ResponseBuffer[1] = BLUENRG_MS_BRD;               
#endif
      
#ifdef STM32WB55xx
      ResponseBuffer[1] = STM32WB55XX_BRD;               
#endif      

      BuffLength = 2;
      break;
    }
  case LIB_VER:
    {
      libVersion = BLEMesh_GetLibraryVersion();
      while(*libVersion != '\0')
      {
        tempBuffer[inc] = *libVersion;
        if(tempBuffer[inc] != 0x2E)
        {
          tempBuffer[inc] = BLEMesh_ModelsASCII_To_Char(tempBuffer[inc]);
          TRACE_M(TF_VENDOR,"Lib version is %x\n\r" ,(unsigned char)tempBuffer[inc]);
        }
        else
        {
          TRACE_M(TF_VENDOR,"Lib version is %c\n\r" ,(unsigned char)tempBuffer[inc]);
        }             
        libVersion++;  
             
        inc++;
      } 
      ResponseBuffer[1]= tempBuffer[0];
      ResponseBuffer[2]= tempBuffer[1];
      ResponseBuffer[3]= tempBuffer[3];
      ResponseBuffer[4]= tempBuffer[4];
      ResponseBuffer[5]= tempBuffer[6];
      ResponseBuffer[6]= tempBuffer[7];
      ResponseBuffer[7]= tempBuffer[8];
      BuffLength = 8;      
      break;
    }
  case LIB_SUB_VER:
    {
      subLibVersion = BLEMesh_GetLibrarySubVersion();
      while(*subLibVersion != '\0')
      {
        tempBuffer[inc] = * subLibVersion;
        if((tempBuffer[inc] != 0x2E) && (tempBuffer[inc] != 0x52))
        {               
          tempBuffer[inc] = BLEMesh_ModelsASCII_To_Char(tempBuffer[inc]);
          TRACE_M(TF_VENDOR,"Sub Lib version is %x\n\r" ,(unsigned char)tempBuffer[inc]);
        }
        else
        {
          TRACE_M(TF_VENDOR,"Sub Lib version is %c\n\r" ,(unsigned char)tempBuffer[inc]);
        } 
        subLibVersion++;  
        inc++;
      } 
      ResponseBuffer[1]= tempBuffer[0];
      ResponseBuffer[2]= tempBuffer[1];
      ResponseBuffer[3]= tempBuffer[3];
      ResponseBuffer[4]= tempBuffer[5];
      ResponseBuffer[5]= tempBuffer[7];
      ResponseBuffer[6]= tempBuffer[9];
      
      BuffLength = 7;
      
      break;
    }
  case APPLICATION_VER:
    {
      status = Appli_Vendor_CopyStringResponse(DISAPP_FIRMWARE_REVISION_NUMBER);
      break;
    }
    
  default:
    {
      status = MOBLE_RESULT_FALSE;
      break;
    }
    
  }
  
  return status;
}


/**
* @brief  Process the Vendor Test Command 
* @param  data: Pointer to the data received from peer_addr
* @param  length: Length of the data
* @retval MOBLE_RESULT status of result
*/          
MOBLE_RESULT Appli_Vendor_Test(MOBLEUINT8 const *data, MOBLEUINT32 length)
{
  MOBLE_RESULT status = MOBLE_RESULT_SUCCESS;  
  MOBLEUINT8 subCmd = data[0];
       /*First Byte is sending the Sub Command*/      
       ResponseBuffer[0]=subCmd;
       TRACE_M(TF_VENDOR,"#01-%02hx! \n\r",data[0]);
  switch(subCmd)
  {             
  case APPLI_TEST_ECHO: 
    {
      if(length > sizeof(ResponseBuffer))
      {
        length = sizeof(ResponseBuffer);
        TRACE_M(TF_VENDOR,"Length received greater than size of response buffer \r\n");
      }
      memcpy (&(ResponseBuffer[1]),&(data[1]),(length-1));
      BuffLength = length;
      break;
    }
  case APPLI_TEST_RANDOMIZATION_RANGE:  
    {
      /*Insert Test related Commands here*/
      break;
    }
  case APPLI_TEST_COUNTER:
    {
#ifdef USER_BOARD_1LED
      if((DUTY <= PWM_TIME_PERIOD) && (DUTY > 1))
      {
        Appli_LightPwmValue.IntensityValue = LED_OFF_VALUE;
        Light_UpdateLedValue(LOAD_STATE , Appli_LightPwmValue);
      }
      else
      {
        Appli_LightPwmValue.IntensityValue = PWM_TIME_PERIOD;
       Light_UpdateLedValue(LOAD_STATE , Appli_LightPwmValue);
      }
        TRACE_M(TF_VENDOR,"Test Counter is running \r\n");
        ResponseBuffer[0] = subCmd;
        ResponseBuffer[1] = Appli_LedState ;
        BuffLength = 2; 
      /*Insert Test related Commands here*/
#endif
      break;
    }
  case APPLI_TEST_INC_COUNTER: 
    {
#ifdef USER_BOARD_1LED
      if((DUTY <= PWM_TIME_PERIOD) && (DUTY > 1))
      {
        Appli_LightPwmValue.IntensityValue = LED_OFF_VALUE;
        Light_UpdateLedValue(LOAD_STATE , Appli_LightPwmValue);
      }
      else
      {
        Appli_LightPwmValue.IntensityValue = PWM_TIME_PERIOD;
        Light_UpdateLedValue(LOAD_STATE , Appli_LightPwmValue);
      }                                                    
              
      TestHitCounter++;              
      TRACE_M(TF_VENDOR,"Command received Count %.2lx \r\n",TestHitCounter);
      ResponseBuffer[0] = subCmd;
      ResponseBuffer[1] = Appli_LedState ;
      BuffLength = 2;
#endif
      /*Insert Test related Commands here*/
      break;
    }
  case APPLI_MODEL_PUBLISH_SELECT:
    {
       for (MOBLEUINT8 idx=0; idx<length; idx++)
       {
         TRACE_I(TF_VENDOR,"data[%d]= %d",idx,data[idx]);  
         TRACE_I(TF_VENDOR,"\n\r");
       } 
       break;
     }
             
  default:
    {
      status = MOBLE_RESULT_FALSE;
      break;
    }
  }
       
  return status;
}


/**
* @brief  Process the Vendor LED Control Command
* @param  data: Pointer to the data received from peer_addr
* @param  length: Length of the data
* @param  elementIndex : selected element where '0' is first element       
* @retval MOBLE_RESULT status of result
*/ 
MOBLE_RESULT Appli_Vendor_LEDControl( MOBLEUINT8 const *data, MOBLEUINT32 length,
                                       MOBLEUINT8 elementIndex , MOBLE_ADDRESS dst_peer)
{
  MOBLE_RESULT status = MOBLE_RESULT_SUCCESS;
  MOBLEUINT8 subCommand; 
  subCommand = data[0];
  MOBLEUINT16 duty;
  MOBLEUINT16 intensityValue = 0;
      
  TRACE_M(TF_VENDOR,"#03-%02hx!\n\r",data[0]);
  switch(subCommand)
  {
    /* 
    Message Received     B0     B1    B2      B3    B4    B5    B6     B7 
    B0 - Sub-Cmd LED
    B1-B7 - Data Bytes       
    */
  case APPLI_CMD_LED_BULB:
    {
      /*User Need to write the commands as per the element selected*/
          
      TRACE_M(TF_VENDOR,"Appli_LED_Control callback received for elementIndex %d \r\n", elementIndex);
      Appli_LedState = *(data+1); /* Toggle the state of the Blue LED */
      if( Appli_LedState == 1)
      {
        BSP_LED_On(LED_BLUE);
      }
      else
      {
        BSP_LED_Off(LED_BLUE);
      }  
      break;
    }
    
    /* Toggle Command */  
  case APPLI_CMD_TOGGLE:
    {
      /*User Need to write the commands as per the element selected*/
          
      TRACE_M(TF_VENDOR,"Appli_LED_Toggle callback received for elementIndex %d \r\n", elementIndex);
      if(Appli_LedState == 1)
      {
        Appli_LightPwmValue.IntensityValue = LED_OFF_VALUE;
                                    
#ifndef CUSTOM_BOARD_PWM_SELECTION            
        Light_UpdateLedValue(LOAD_STATE , Appli_LightPwmValue);   /* PWM_ID = PWM4, mapped on PWM4_PIN (GPIO_14 in mapping) */
#else
        Light_UpdateLedValue(RESET_STATE , Appli_LightPwmValue);   /* PWM_ID = PWM4, mapped on PWM4_PIN (GPIO_14 in mapping) */
#endif            
        Appli_LedState = 0;
        BSP_LED_Off(LED_BLUE);
      }
      else
      {
        Appli_LightPwmValue.IntensityValue = PWM_TIME_PERIOD;
        Light_UpdateLedValue(LOAD_STATE , Appli_LightPwmValue);
        Appli_LedState = 1;
        BSP_LED_On(LED_BLUE);
      }
              
      break;
    }
    /* On Command */  
  case APPLI_CMD_ON:
    {
      /*User Need to write the commands as per the element selected*/
          
      TRACE_M(TF_VENDOR,"Appli_LED_ON callback received for elementIndex %d \r\n", elementIndex);     
      Appli_LightPwmValue.IntensityValue = PWM_TIME_PERIOD;
      Light_UpdateLedValue(LOAD_STATE , Appli_LightPwmValue);   /* PWM_ID = PWM4, mapped on PWM4_PIN (GPIO_14 in mapping) */
      BSP_LED_On(LED_BLUE);
      Appli_LedState = 1;
          
      break;
    }
    /* Off Command */  
  case APPLI_CMD_OFF:
    {
      /*User Need to write the commands as per the element selected*/
          
      TRACE_M(TF_VENDOR,"Appli_LED_OFF callback received for elementIndex %d \r\n", elementIndex);                   
      Appli_LightPwmValue.IntensityValue = LED_OFF_VALUE;
            
#ifndef CUSTOM_BOARD_PWM_SELECTION            
      Light_UpdateLedValue(LOAD_STATE , Appli_LightPwmValue);   /* PWM_ID = PWM4, mapped on PWM4_PIN (GPIO_14 in mapping) */
#else
      Light_UpdateLedValue(RESET_STATE , Appli_LightPwmValue);   /* PWM_ID = PWM4, mapped on PWM4_PIN (GPIO_14 in mapping) */
#endif            
      Appli_LedState = 0;
      BSP_LED_Off(LED_BLUE);
          
      break;
    }
        /* intensity command */
    case APPLI_CMD_LED_INTENSITY:
      {
        /*User Need to write the commands as per the element selected*/
          
        TRACE_M(TF_VENDOR,"Appli_LED_Intensity callback received for elementIndex %d \r\n", elementIndex);    
        intensityValue = data[2] << 8;
        intensityValue |= data[1];
                    
        duty = PwmValueMapping(intensityValue , 0x7FFF ,0);                         
        Appli_LightPwmValue.IntensityValue = duty;
        Light_UpdateLedValue(LOAD_STATE , Appli_LightPwmValue);             
        if(duty > 16000)
        {
          BSP_LED_On(LED_BLUE);
        }
        else
        {
          BSP_LED_Off(LED_BLUE);
        }
        break;
      }
    /* Default case - Not valid command */
  default:
    {
      status = MOBLE_RESULT_FALSE;
      break;
    }
  }
  /*Buffer will be sent for Reliable Response*/
  /*First Byte is Sub Command and 2nd Byte is LED Status*/
  ResponseBuffer[0] = subCommand;
  if(subCommand == APPLI_CMD_LED_INTENSITY)
  {
    ResponseBuffer[1] = intensityValue >> 8 ;
    ResponseBuffer[2] = intensityValue ;
    BuffLength = 3;
  }
  else
  {
    ResponseBuffer[1] = Appli_LedState ;
    BuffLength = 2; 
  }
      
  return status;
}

/**
* @brief  Process the Vendor Data write Command 
* @param  data: Pointer to the data received from peer_addr
* @param  length: Length of the data
* @retval MOBLE_RESULT status of result
*/          
MOBLE_RESULT Appli_Vendor_Data_write(MOBLEUINT8 const *data, MOBLEUINT32 length)
{
  MOBLE_RESULT status = MOBLE_RESULT_SUCCESS;
  MOBLEUINT8 subCmd;
  MOBLEUINT8 tmp_data[VENDOR_DATA_BUFFER_SIZE - 1U];
  MOBLEUINT8 tmp_len;

  if ((data == NULL) || (length == 0U))
  {
    return MOBLE_RESULT_INVALIDARG;
  }

  subCmd = data[0];
  /*First Byte is sending the Sub Command*/      
  ResponseBuffer[0] = subCmd;
  BuffLength = 1U;
       
  TRACE_M(TF_VENDOR, "#0E-%02hx %02lx! \n\r",data[0], length);
  for(MOBLEUINT16 i=0; i<length; i++)
  {
    TRACE_I(TF_VENDOR,"%02hx ",data[i]);
  }
  TRACE_I(TF_VENDOR,"\n\r");
  switch(subCmd)
  {     
    case APPLI_STRING_WRITE:
    {
      if (length > sizeof(ResponseBuffer))
      {
        status = MOBLE_RESULT_INVALIDARG;
        break;
      }

      memcpy(&ResponseBuffer, data, length);
      BuffLength = (MOBLEUINT16)length;
      break;
    }

    case APPLI_SHOT_READ:
    {
      tmp_len = GetLastShotNotification(tmp_data, sizeof(tmp_data));
      if (tmp_len == 0U)
      {
        status = MOBLE_RESULT_FALSE;
        break;
      }

      memcpy(&ResponseBuffer[1], tmp_data, tmp_len);
      BuffLength = (MOBLEUINT16)(1U + tmp_len);
      break;
    }

    case APPLI_TARGET_READ:
    {
      tmp_len = GetLastTargetNotification(tmp_data, sizeof(tmp_data));
      if (tmp_len == 0U)
      {
        status = MOBLE_RESULT_FALSE;
        break;
      }

      memcpy(&ResponseBuffer[1], tmp_data, tmp_len);
      BuffLength = (MOBLEUINT16)(1U + tmp_len);
      break;
    }

    case APPLI_BATTERY_READ:
    {
      ResponseBuffer[1] = GetLastBatteryLevel();
      BuffLength = 2U;
      break;
    }

    case APPLI_SHOT_QUEUE_STATUS:
    {
      MOBLEUINT16 count = GetShotQueueCount();
      MOBLEUINT32 oldest = GetShotQueueOldestSequence();
      MOBLEUINT32 next = GetShotQueueNextSequence();

      if (length != 1U)
      {
        status = MOBLE_RESULT_INVALIDARG;
        break;
      }

      ResponseBuffer[1] = (MOBLEUINT8)(count & 0xFFU);
      ResponseBuffer[2] = (MOBLEUINT8)((count >> 8) & 0xFFU);
      ResponseBuffer[3] = (MOBLEUINT8)(oldest & 0xFFU);
      ResponseBuffer[4] = (MOBLEUINT8)((oldest >> 8) & 0xFFU);
      ResponseBuffer[5] = (MOBLEUINT8)((oldest >> 16) & 0xFFU);
      ResponseBuffer[6] = (MOBLEUINT8)((oldest >> 24) & 0xFFU);
      ResponseBuffer[7] = (MOBLEUINT8)(next & 0xFFU);
      ResponseBuffer[8] = (MOBLEUINT8)((next >> 8) & 0xFFU);
      ResponseBuffer[9] = (MOBLEUINT8)((next >> 16) & 0xFFU);
      ResponseBuffer[10] = (MOBLEUINT8)((next >> 24) & 0xFFU);
      BuffLength = 11U;
      break;
    }

    case APPLI_SHOT_QUEUE_READ:
    {
      MOBLEUINT32 sequence;

      if (length != 5U)
      {
        status = MOBLE_RESULT_INVALIDARG;
        break;
      }

      sequence = (MOBLEUINT32)data[1]
               | ((MOBLEUINT32)data[2] << 8)
               | ((MOBLEUINT32)data[3] << 16)
               | ((MOBLEUINT32)data[4] << 24);
      if (!GetShotBySequence(sequence, tmp_data, sizeof(tmp_data)))
      {
        status = MOBLE_RESULT_FALSE;
        break;
      }

      ResponseBuffer[1] = data[1];
      ResponseBuffer[2] = data[2];
      ResponseBuffer[3] = data[3];
      ResponseBuffer[4] = data[4];
      memcpy(&ResponseBuffer[5], tmp_data, NOTIFY_SHOT_SIZE);
      BuffLength = 5U + NOTIFY_SHOT_SIZE;
      break;
    }

    case APPLI_RAW_READ:
    {
      if (length != 1U)
      {
        status = MOBLE_RESULT_INVALIDARG;
        break;
      }

      tmp_len = P2PS_GetLastRawNotification(tmp_data, sizeof(tmp_data));
      if (tmp_len != NOTIFY_RAW_SIZE)
      {
        status = MOBLE_RESULT_FALSE;
        break;
      }

      memcpy(&ResponseBuffer[1], tmp_data, tmp_len);
      BuffLength = (MOBLEUINT16)(1U + tmp_len);
      break;
    }

    case APPLI_HW_REV_READ:
    {
      status = (length == 1U)
          ? Appli_Vendor_CopyStringResponse(DISAPP_HARDWARE_REVISION_NUMBER)
          : MOBLE_RESULT_INVALIDARG;
      break;
    }

    case APPLI_FW_VER_READ:
    {
      status = (length == 1U)
          ? Appli_Vendor_CopyStringResponse(DISAPP_FIRMWARE_REVISION_NUMBER)
          : MOBLE_RESULT_INVALIDARG;
      break;
    }

    case APPLI_MODEL_READ:
    {
      status = (length == 1U)
          ? Appli_Vendor_CopyStringResponse(DISAPP_MODEL_NUMBER)
          : MOBLE_RESULT_INVALIDARG;
      break;
    }

    case APPLI_MANUFACTURER_READ:
    {
      status = (length == 1U)
          ? Appli_Vendor_CopyStringResponse(DISAPP_MANUFACTURER_NAME)
          : MOBLE_RESULT_INVALIDARG;
      break;
    }

    case APPLI_UART_CMD:
    {
      /* Forward 1 byte vers UART (RESET=0x01, JN=0x02, ETAT_CIBLE=0x03, REVEIL=0x04) */
      if (length < 2U)
      {
        status = MOBLE_RESULT_INVALIDARG;
        break;
      }
      MOBLEUINT8 uart_byte = data[1];
      HAL_UART_Transmit(&hlpuart1, &uart_byte, 1U, 1000U);
      break;
    }

    case APPLI_TIMER_SET:
    {
      /* [subcmd, type(0x03=Finish/0x04=NotFinish), value_seconds] */
      if (length < 3U)
      {
        status = MOBLE_RESULT_INVALIDARG;
        break;
      }
      if (data[1] == 0x03U)
      {
        SetResetTimerValFinish(data[2]);
      }
      else if (data[1] == 0x04U)
      {
        SetResetTimerValNotFinish(data[2]);
      }
      else
      {
        status = MOBLE_RESULT_INVALIDARG;
      }
      break;
    }

    case APPLI_TIMER_READ:
    {
      /* [subcmd, type(0x03=Finish/0x04=NotFinish)] → répond valeur */
      if (length < 2U)
      {
        status = MOBLE_RESULT_INVALIDARG;
        break;
      }
      if (data[1] == 0x03U)
      {
        ResponseBuffer[1] = GetResetTimerValFinish();
      }
      else if (data[1] == 0x04U)
      {
        ResponseBuffer[1] = GetResetTimerValNotFinish();
      }
      else
      {
        status = MOBLE_RESULT_INVALIDARG;
        break;
      }
      BuffLength = 2U;
      break;
    }

    case APPLI_MODE_SET:
    {
      /* [subcmd, mode_value] */
      if (length < 2U)
      {
        status = MOBLE_RESULT_INVALIDARG;
        break;
      }
      SetMode(data[1]);
      break;
    }

    case APPLI_MODE_READ:
    {
      /* répond avec la valeur de mode courante */
      ResponseBuffer[1] = GetMode();
      BuffLength = 2U;
      break;
    }

    case APPLI_SSID_SET:
    {
      /* [subcmd, ...string... (max 16 chars)] */
      if (length < 2U || length > 17U)
      {
        status = MOBLE_RESULT_INVALIDARG;
        break;
      }
      char ssid_buf[17];
      MOBLEUINT8 ssid_len = (MOBLEUINT8)(length - 1U);
      memset(ssid_buf, 0, sizeof(ssid_buf));
      memcpy(ssid_buf, &data[1], ssid_len);
      SetSSIDName(ssid_buf);
      break;
    }

    case APPLI_SSID_READ:
    {
      /* répond avec le SSID stocké en EEPROM */
      const char *ssid = GetSSIDName();
      MOBLEUINT8 ssid_len = (MOBLEUINT8)strlen(ssid);
      if (ssid_len > 16U) ssid_len = 16U;
      memcpy(&ResponseBuffer[1], ssid, ssid_len);
      BuffLength = (MOBLEUINT16)(1U + ssid_len);
      break;
    }

    case APPLI_ENTER_GATT_MAINTENANCE:
    {
      uint32_t transition_id;

      if (length != 5U)
      {
        status = MOBLE_RESULT_INVALIDARG;
        ResponseBuffer[1] = 0x01U;
        BuffLength = 2U;
        break;
      }

      transition_id = (uint32_t)data[1]
                    | ((uint32_t)data[2] << 8)
                    | ((uint32_t)data[3] << 16)
                    | ((uint32_t)data[4] << 24);
      if (!ModeManager_RequestMode(BOOT_MODE_GATT_MAINTENANCE, transition_id))
      {
        status = MOBLE_RESULT_FALSE;
        ResponseBuffer[1] = 0x04U;
        BuffLength = 2U;
        break;
      }

      ResponseBuffer[1] = 0x00U;
      memcpy(&ResponseBuffer[2], &data[1], 4U);
      BuffLength = 6U;
      ModeManager_ScheduleReset();
      break;
    }

    case APPLI_GET_MODE_STATUS:
    {
      ModeStatus_t mode_status;

      if (length != 1U)
      {
        status = MOBLE_RESULT_INVALIDARG;
        break;
      }

      ModeManager_GetStatus(&mode_status);
      ResponseBuffer[1] = MODE_PROTOCOL_VERSION;
      ResponseBuffer[2] = mode_status.actual_mode;
      ResponseBuffer[3] = mode_status.requested_mode;
      ResponseBuffer[4] = mode_status.transition_in_progress;
      ResponseBuffer[5] = mode_status.provision_result;
      ResponseBuffer[6] = mode_status.last_error;
      ResponseBuffer[7] = mode_status.consecutive_unplanned_resets;
      ResponseBuffer[8] = (uint8_t)(mode_status.transition_id & 0xFFU);
      ResponseBuffer[9] = (uint8_t)((mode_status.transition_id >> 8) & 0xFFU);
      ResponseBuffer[10] = (uint8_t)((mode_status.transition_id >> 16) & 0xFFU);
      ResponseBuffer[11] = (uint8_t)((mode_status.transition_id >> 24) & 0xFFU);
      BuffLength = 12U;
      break;
    }

    default:
    {
      status = MOBLE_RESULT_FALSE;
      break;
    }
  }
  return status;        
}
         
/**
* @brief  Appli_GetTestValue: This function is callback for Application
*          when Vensor application test command received then status message is to be provided
* @param  responseValue: Pointer to the status message
* @retval void
*/ 
void Appli_GetTestValue (MOBLEUINT8 *responseValue)
{
  *responseValue = TestHitCounter;
  *(responseValue+1)  = TestHitCounter >> 8;
  *(responseValue+2)  = TestHitCounter >> 16;
  *(responseValue+3)  = TestHitCounter >> 24;
  TestHitCounter = 0;
}
/**
* @brief  Process the Vendor LED Control Command
* @param  data: Pointer to the data received from peer_addr
* @param  length: Length of the data
* @param  elementIndex : selected element where '0' is first element       
* @param  dst_peer : Destination address
* @retval MOBLE_RESULT status of result
*/ 
MOBLE_RESULT Appli_Vendor_SetBigDataPacket(MOBLEUINT8 *data, MOBLEUINT32 length,
                                       MOBLEUINT8 elementIndex , MOBLE_ADDRESS dst_peer)
{
  MOBLE_RESULT status = MOBLE_RESULT_SUCCESS;
  
  if (length > VENDOR_DATA_BUFFER_SIZE)
  {
    status = MOBLE_RESULT_INVALIDARG;
  }
  memmove(Appli_VendorBigData.data, data, length);
  Appli_VendorBigData.dst = dst_peer;
  Appli_VendorBigData.length = length;
  Appli_VendorBigData.elementIndex = elementIndex;
  return status;
}
/**
* @brief  Send Vendor big data packet
* @param  void      
* @retval MOBLE_RESULT status of result
*/ 
MOBLE_RESULT Appli_Vendor_SendBigDataPacket(void)
{
  MOBLE_RESULT status = MOBLE_RESULT_SUCCESS;
  
  if(BLEMesh_TrsptIsBusyState())
  {
    BSP_LED_On(LED_BLUE);
    status = MOBLE_RESULT_FALSE;
  }
  else
  {
    BSP_LED_Off(LED_BLUE);
    status = BLEMesh_SetRemoteData(Appli_VendorBigData.dst,
                                       Appli_VendorBigData.elementIndex,
                                       0x000E,                                   
                                       Appli_VendorBigData.data, 
                                       Appli_VendorBigData.length,
                                       MOBLE_FALSE, 
                                       MOBLE_TRUE);

  }
  return status;
}

/**
* @brief  Publish Command for Vendor Model
* @param  srcAddress: Source Address of Node 
* @retval void
*/          
void Appli_Vendor_Publish(MOBLE_ADDRESS srcAddress)
{
  MOBLE_RESULT result = MOBLE_RESULT_SUCCESS;
  MOBLEUINT8 AppliBuff[1];
  /* changes the LED status on other nodes in the network */
    
  AppliBuff[0] = APPLI_CMD_TOGGLE;
    
  result = BLEMesh_SetRemotePublication(VENDORMODEL_STMICRO_ID1, srcAddress,
                                            APPLI_LED_CONTROL_STATUS_CMD, 
                                            AppliBuff, sizeof(AppliBuff),
                                            MOBLE_FALSE, MOBLE_TRUE);
  
  if(result)
  {
    TRACE_I(TF_VENDOR_M, "Publication Error \r\n");
  }
}
/**
* @}
*/

/**
* @}
*/


