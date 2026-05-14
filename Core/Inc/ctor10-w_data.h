/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * File Name          : App/ctor10-w_data.h
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
#ifndef __CTOR10_W_DATA_H
#define __CTOR10_W_DATA_H
#include <stdint.h>
/*#ifdef __cplusplus
extern "C" {
#endif*/



void ctor10wStoreAndNotify (uint8_t *Rx_data, int lenght);
void ctor10w_data_Init(void);
void SetResetTimerFinish(uint8_t FinishTimer);
void SetResetTimerNotFinish(uint8_t NotFinishTimer);
void GPIOVisualStateDisable(void);
void GPIOVisualStateEnable(void);
void GetTabShot(void);
void GetHardwareVersion(void);

extern char DISAPP_HARDWARE_REVISION_NUMBER[6];

#endif /* __CTOR10_W_DATA_H */
