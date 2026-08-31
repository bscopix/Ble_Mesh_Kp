/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    App/p2p_server_app.h
  * @author  MCD Application Team
  * @brief   Header for p2p_server_app.c module
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef P2P_SERVER_APP_H
#define P2P_SERVER_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "main.h"
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
typedef enum
{
  PEER_CONN_HANDLE_EVT,
  PEER_DISCON_HANDLE_EVT,
} P2PS_APP__Opcode_Notification_evt_t;

typedef struct
{
  P2PS_APP__Opcode_Notification_evt_t   P2P_Evt_Opcode;
  uint16_t                              ConnectionHandle;
}P2PS_APP_ConnHandle_Not_evt_t;
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* External variables --------------------------------------------------------*/
/* USER CODE BEGIN EV */
typedef struct
 {
   uint8_t               ShotNotification_Status; /* used to chek if P2P Server is enabled to Notify */
	 uint8_t               RawNotification_Status;
	 uint8_t               TargetNotification_Status;
   uint16_t              ConnectionHandle;
   uint8_t 				RawTargetToNotification[NOTIFY_RAW_SIZE];
   uint8_t 				ShotToNotification[NOTIFY_SHOT_SIZE];
   uint8_t 				TargetToNotification[NOTIFY_TARGET_SIZE];
 } P2P_Server_App_Context_t;
/* USER CODE END EV */

/* Exported macros ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions ---------------------------------------------*/
  void P2PS_APP_Init( void );
  void P2PS_APP_Notification( P2PS_APP_ConnHandle_Not_evt_t *pNotification );
/* USER CODE BEGIN EF */
  void storeRawData(uint8_t *data, int lenght );
  void storeShotData(uint8_t *data, int lenght );
  void storeTargetData(uint8_t *data, int lenght );
  uint8_t P2PS_GetLastRawNotification(uint8_t *dst, uint8_t max_len);
extern P2P_Server_App_Context_t P2P_Server_App_Context;
/* USER CODE END EF */

#ifdef __cplusplus
}
#endif

#endif /*P2P_SERVER_APP_H */
