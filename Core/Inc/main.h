/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32wbxx_hal.h"
#include "app_conf.h"
#include "app_entry.h"
#include "app_common.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */
#define TXBUFLEN (250)
#define I2C_MAX_DELAY 1000 // en millisecondes
#define AddPrivateEepromStart 0x0000
#define AddPublicEepromStart  0x0016
/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */
extern char txBuf[TXBUFLEN];
extern char rxBuf[TXBUFLEN];
extern UART_HandleTypeDef huart1;
extern CRC_HandleTypeDef hcrc;
extern I2C_HandleTypeDef hi2c1;
/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define Aux_out_1_Pin GPIO_PIN_0
#define Aux_out_1_GPIO_Port GPIOA
#define NFC_SDA_Pin GPIO_PIN_9
#define NFC_SDA_GPIO_Port GPIOB
#define NFC_SCL_Pin GPIO_PIN_8
#define NFC_SCL_GPIO_Port GPIOB
#define NFC_IT_Pin GPIO_PIN_5
#define NFC_IT_GPIO_Port GPIOB
#define NFC_IT_EXTI_IRQn EXTI9_5_IRQn
#define led_target_5_Pin GPIO_PIN_11
#define led_target_5_GPIO_Port GPIOC
#define HW_Version_0_Pin GPIO_PIN_0
#define HW_Version_0_GPIO_Port GPIOD
#define HW_Version_1_Pin GPIO_PIN_1
#define HW_Version_1_GPIO_Port GPIOD
#define HW_Version_2_Pin GPIO_PIN_13
#define HW_Version_2_GPIO_Port GPIOB
#define led_target_4_Pin GPIO_PIN_13
#define led_target_4_GPIO_Port GPIOD
#define led_target_3_Pin GPIO_PIN_12
#define led_target_3_GPIO_Port GPIOD
#define led_target_2_Pin GPIO_PIN_9
#define led_target_2_GPIO_Port GPIOC
#define led_target_1_Pin GPIO_PIN_3
#define led_target_1_GPIO_Port GPIOD

/* USER CODE BEGIN Private defines */
#define NOTIFY_SHOT_SIZE 13
#define NOTIFY_TARGET_SIZE 4
#define NOTIFY_RAW_SIZE 20
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
