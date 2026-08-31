/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32wbxx_it.c
  * @brief   Interrupt Service Routines.
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
#include "stm32wbxx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "dbg_trace.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

typedef struct
{
  uint32_t magic;
  uint32_t msp;
  uint32_t psp;
  uint32_t cfsr;
  uint32_t hfsr;
  uint32_t dfsr;
  uint32_t afsr;
  uint32_t bfar;
  uint32_t mmfar;
  uint32_t shcsr;
  uint32_t icsr;

  uint32_t msp_r0;
  uint32_t msp_r1;
  uint32_t msp_r2;
  uint32_t msp_r3;
  uint32_t msp_r12;
  uint32_t msp_lr;
  uint32_t msp_pc;
  uint32_t msp_xpsr;

  uint32_t psp_r0;
  uint32_t psp_r1;
  uint32_t psp_r2;
  uint32_t psp_r3;
  uint32_t psp_r12;
  uint32_t psp_lr;
  uint32_t psp_pc;
  uint32_t psp_xpsr;
} FaultContext_t;

__attribute__((section("FAULT_NOINIT"), used, aligned(8)))
volatile FaultContext_t g_FaultContext;

static void Fault_CopyStackFrame(uint32_t sp,
                                 uint32_t *r0,
                                 uint32_t *r1,
                                 uint32_t *r2,
                                 uint32_t *r3,
                                 uint32_t *r12,
                                 uint32_t *lr,
                                 uint32_t *pc,
                                 uint32_t *xpsr)
{
  /* Wide SRAM range for STM32WB user RAM banks. */
  if (((sp & 0x3U) == 0U) &&
      (sp >= 0x20000000UL) &&
      (sp <= 0x2004FFF0UL))
  {
    const uint32_t *stack = (const uint32_t *)sp;
    *r0 = stack[0];
    *r1 = stack[1];
    *r2 = stack[2];
    *r3 = stack[3];
    *r12 = stack[4];
    *lr = stack[5];
    *pc = stack[6];
    *xpsr = stack[7];
  }
}

void FaultContext_ReportAndClear(void)
{
  if (g_FaultContext.magic == 0xDEADFA11UL)
  {
    APP_ESSENTIAL_MSG("[FAULT] CFSR=%08lx HFSR=%08lx BFAR=%08lx MMFAR=%08lx\r\n",
                (unsigned long)g_FaultContext.cfsr,
                (unsigned long)g_FaultContext.hfsr,
                (unsigned long)g_FaultContext.bfar,
                (unsigned long)g_FaultContext.mmfar);
    APP_ESSENTIAL_MSG("[FAULT] MSP=%08lx PC=%08lx LR=%08lx PSP=%08lx PC=%08lx LR=%08lx\r\n",
                (unsigned long)g_FaultContext.msp,
                (unsigned long)g_FaultContext.msp_pc,
                (unsigned long)g_FaultContext.msp_lr,
                (unsigned long)g_FaultContext.psp,
                (unsigned long)g_FaultContext.psp_pc,
                (unsigned long)g_FaultContext.psp_lr);
    g_FaultContext.magic = 0U;
  }
}

uint8_t FaultContext_IsPending(void)
{
  return (g_FaultContext.magic == 0xDEADFA11UL) ? 1U : 0U;
}

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_HandleTypeDef hdma_usart1_tx;
extern UART_HandleTypeDef hlpuart1;
extern UART_HandleTypeDef huart1;
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  g_FaultContext.magic = 0xDEADFA11UL;
  g_FaultContext.msp = __get_MSP();
  g_FaultContext.psp = __get_PSP();

  g_FaultContext.cfsr = SCB->CFSR;
  g_FaultContext.hfsr = SCB->HFSR;
  g_FaultContext.dfsr = SCB->DFSR;
  g_FaultContext.afsr = SCB->AFSR;
  g_FaultContext.bfar = SCB->BFAR;
  g_FaultContext.mmfar = SCB->MMFAR;
  g_FaultContext.shcsr = SCB->SHCSR;
  g_FaultContext.icsr = SCB->ICSR;

  Fault_CopyStackFrame(g_FaultContext.msp,
                       (uint32_t *)&g_FaultContext.msp_r0,
                       (uint32_t *)&g_FaultContext.msp_r1,
                       (uint32_t *)&g_FaultContext.msp_r2,
                       (uint32_t *)&g_FaultContext.msp_r3,
                       (uint32_t *)&g_FaultContext.msp_r12,
                       (uint32_t *)&g_FaultContext.msp_lr,
                       (uint32_t *)&g_FaultContext.msp_pc,
                       (uint32_t *)&g_FaultContext.msp_xpsr);

  Fault_CopyStackFrame(g_FaultContext.psp,
                       (uint32_t *)&g_FaultContext.psp_r0,
                       (uint32_t *)&g_FaultContext.psp_r1,
                       (uint32_t *)&g_FaultContext.psp_r2,
                       (uint32_t *)&g_FaultContext.psp_r3,
                       (uint32_t *)&g_FaultContext.psp_r12,
                       (uint32_t *)&g_FaultContext.psp_lr,
                       (uint32_t *)&g_FaultContext.psp_pc,
                       (uint32_t *)&g_FaultContext.psp_xpsr);

  if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U)
  {
    __BKPT(0);
  }

  __DSB();
  NVIC_SystemReset();

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  HardFault_Handler();
  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Prefetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  HardFault_Handler();
  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  HardFault_Handler();
  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVCall_IRQn 0 */

  /* USER CODE END SVCall_IRQn 0 */
  /* USER CODE BEGIN SVCall_IRQn 1 */

  /* USER CODE END SVCall_IRQn 1 */
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */

  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */

  /* USER CODE END PendSV_IRQn 1 */
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32WBxx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32wbxx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles EXTI line[9:5] interrupts.
  */
void EXTI9_5_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI9_5_IRQn 0 */

  /* USER CODE END EXTI9_5_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(NFC_IT_Pin);
  /* USER CODE BEGIN EXTI9_5_IRQn 1 */

  /* USER CODE END EXTI9_5_IRQn 1 */
}

/**
  * @brief This function handles USART1 global interrupt.
  */
void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */

  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(&huart1);
  /* USER CODE BEGIN USART1_IRQn 1 */

  /* USER CODE END USART1_IRQn 1 */
}

/**
  * @brief This function handles LPUART1 global interrupt.
  */
void LPUART1_IRQHandler(void)
{
  /* USER CODE BEGIN LPUART1_IRQn 0 */

  /* USER CODE END LPUART1_IRQn 0 */
  HAL_UART_IRQHandler(&hlpuart1);
  /* USER CODE BEGIN LPUART1_IRQn 1 */

  /* USER CODE END LPUART1_IRQn 1 */
}

/**
  * @brief This function handles HSEM global interrupt.
  */
void HSEM_IRQHandler(void)
{
  /* USER CODE BEGIN HSEM_IRQn 0 */

  /* USER CODE END HSEM_IRQn 0 */
  HAL_HSEM_IRQHandler();
  /* USER CODE BEGIN HSEM_IRQn 1 */

  /* USER CODE END HSEM_IRQn 1 */
}

/**
  * @brief This function handles DMA2 channel4 global interrupt.
  */
void DMA2_Channel4_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Channel4_IRQn 0 */

  /* USER CODE END DMA2_Channel4_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart1_tx);
  /* USER CODE BEGIN DMA2_Channel4_IRQn 1 */

  /* USER CODE END DMA2_Channel4_IRQn 1 */
}

/* USER CODE BEGIN 1 */
void IPCC_C1_RX_IRQHandler(void)
{

  HAL_IPCC_RX_IRQHandler(&hipcc);
}
void IPCC_C1_TX_IRQHandler(void)
{
  HAL_IPCC_TX_IRQHandler(&hipcc);
}
void RTC_WKUP_IRQHandler(void)
{
  HAL_RTCEx_WakeUpTimerIRQHandler();
}
/* USER CODE END 1 */
