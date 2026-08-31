/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_entry.c
  * @author  MCD Application Team
  * @brief   Entry point of the application
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
#include "app_common.h"
#include "main.h"
#include "app_entry.h"
#include "app_ble.h"
#include "ble.h"
#include "ble_gatt_aci.h"
#include "tl.h"
#include "stm32_seq.h"
#include "shci_tl.h"
#include "stm32_lpm.h"
#include "app_debug.h"
#include "dbg_trace.h"
#include "shci.h"
#include "otp.h"
#include "appli_mesh.h"
#include "appli_nvm.h"
#include "pal_nvm.h"
#include "mesh_cfg.h"
#include "mode_manager.h"

/* Private includes -----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
extern RTC_HandleTypeDef hrtc;
extern const void *mobleNvmBase;
extern const void *appNvmBase;
extern const void *modeNvmBase;
extern const void *prvsnr_data;

/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private defines -----------------------------------------------------------*/
#define POOL_SIZE (CFG_TLBLE_EVT_QUEUE_LENGTH*4U*DIVC((sizeof(TL_PacketHeader_t) + TL_BLE_EVENT_FRAME_SIZE), 4U))

/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macros ------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t EvtPool[POOL_SIZE];
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static TL_CmdPacket_t SystemCmdBuffer;
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t SystemSpareEvtBuffer[sizeof(TL_PacketHeader_t) + TL_EVT_HDR_SIZE + 255U];
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t BleSpareEvtBuffer[sizeof(TL_PacketHeader_t) + TL_EVT_HDR_SIZE + 255];
EXTI_HandleTypeDef exti_handle;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private functions prototypes-----------------------------------------------*/
static void Config_HSE(void);
static void Reset_Device(void);
#if (CFG_HW_RESET_BY_FW == 1)
static void Reset_IPCC(void);
static void Reset_BackupDomain(void);
#endif /* CFG_HW_RESET_BY_FW == 1*/
static void System_Init(void);
static void SystemPower_Config(void);
static void appe_Tl_Init(void);
static void APPE_SysStatusNot(SHCI_TL_CmdStatus_t status);
static void APPE_SysUserEvtRx(void * pPayload);
static void APPE_SysEvtReadyProcessing(void * pPayload);
static void APPE_SysEvtError(void * pPayload);
static void Init_Rtc(void);
static const char *APPE_GetStackTypeString(uint8_t stackType);
static void APPE_IwdgInit(void);
static void APPE_IwdgWakeup(void);

static IWDG_HandleTypeDef happe_iwdg;
static uint8_t appe_iwdg_timer_id;

#define APPE_IWDG_WAKEUP_INTERVAL (10UL * (1000000UL / CFG_TS_TICK_VAL))

/* BLE Mesh library compatibility: map legacy permit_read API to current allow/deny APIs. */
tBleStatus aci_gatt_permit_read(uint16_t Connection_Handle,
                                uint8_t Read_status,
                                uint8_t Error_Code,
                                uint16_t Attr_Handle)
{
  (void)Attr_Handle;

  if (Read_status != 0U)
  {
    return aci_gatt_deny_read(Connection_Handle, Error_Code);
  }

  return aci_gatt_allow_read(Connection_Handle);
}

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Functions Definition ------------------------------------------------------*/
void MX_APPE_Config(void)
{
  /**
   * The OPTVERR flag is wrongly set at power on
   * It shall be cleared before using any HAL_FLASH_xxx() api
   */
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPTVERR);

  /**
   * Reset some configurations so that the system behave in the same way
   * when either out of nReset or Power On
   */
  Reset_Device();

  /* Configure HSE Tuning */
  Config_HSE();

  /* Start recovery coverage before peripheral/NFC/application init. The long
   * timeout leaves normal boot ample time but breaks out of early init loops. */
  APPE_IwdgInit();

  return;
}

void MX_APPE_Init(void)
{
#if defined(STM32WB15xx)
  uint32_t last_user_flash_address = ((READ_BIT(FLASH->SFR, FLASH_SFR_SFSA) >> FLASH_SFR_SFSA_Pos) << 11) + FLASH_BASE;
  const uint32_t app_nvm_size = 2048U;
  const uint32_t prvn_nvm_page_size = 2048U;
  const uint32_t mode_nvm_size = 4096U;
#elif defined(STM32WB55xx) || defined(STM32WB5Mxx)
  uint32_t last_user_flash_address = ((READ_BIT(FLASH->SFR, FLASH_SFR_SFSA) >> FLASH_SFR_SFSA_Pos) << 12) + FLASH_BASE;
  const uint32_t app_nvm_size = 4096U;
  const uint32_t prvn_nvm_page_size = 4096U;
  const uint32_t mode_nvm_size = 8192U;
#else
  uint32_t last_user_flash_address = FLASH_BASE + FLASH_SIZE;
  const uint32_t app_nvm_size = 4096U;
  const uint32_t prvn_nvm_page_size = 4096U;
  const uint32_t mode_nvm_size = 8192U;
#endif

  System_Init();       /**< System initialization */

  SystemPower_Config(); /**< Configure the system Power Mode */

  HW_TS_Init(hw_ts_InitMode_Full, &hrtc); /**< Initialize the TimerServer */
  /* The callback intentionally does not feed the watchdog. It only wakes CPU1
   * from long low-power idle; the sequencer must return before refresh. */
  HW_TS_Create(CFG_TIM_PROC_ID_ISR,
               &appe_iwdg_timer_id,
               hw_ts_Repeated,
               APPE_IwdgWakeup);
  HW_TS_Start(appe_iwdg_timer_id, APPE_IWDG_WAKEUP_INTERVAL);

/* USER CODE BEGIN APPE_Init_1 */
	APPD_Init();

  /* Keep Mesh library NVM pages in the user flash area, as in reference project. */
  mobleNvmBase = (const void *)(last_user_flash_address - NVM_SIZE);
  appNvmBase   = (const void *)(last_user_flash_address - NVM_SIZE - app_nvm_size);
  prvsnr_data  = (const void *)(last_user_flash_address - NVM_SIZE - app_nvm_size - prvn_nvm_page_size);
  /* Two application-owned pages form an atomic journal for mode transitions.
   * Existing Mesh/application/provisioner NVM addresses remain unchanged. */
  modeNvmBase  = (const void *)(last_user_flash_address - NVM_SIZE - app_nvm_size
                                - prvn_nvm_page_size - mode_nvm_size);

  TRACE_I(TF_INIT,"NVM bases: mesh=0x%08lx app=0x%08lx prv=0x%08lx mode=0x%08lx\r\n",
          (unsigned long)mobleNvmBase,
          (unsigned long)appNvmBase,
          (unsigned long)prvsnr_data,
          (unsigned long)modeNvmBase);
/* USER CODE END APPE_Init_1 */
  appe_Tl_Init();	/* Initialize all transport layers */

  /**
   * From now, the application is waiting for the ready event (VS_HCI_C2_Ready)
   * received on the system channel before starting the Stack
   * This system event is received with APPE_SysUserEvtRx()
   */
/* USER CODE BEGIN APPE_Init_2 */

/* USER CODE END APPE_Init_2 */

   return;
}

void Init_Smps(void)
{
#if (CFG_USE_SMPS != 0)
  /**
   *  Configure and enable SMPS
   *
   *  The SMPS configuration is not yet supported by CubeMx
   *  when SMPS output voltage is set to 1.4V, the RF output power is limited to 3.7dBm
   *  the SMPS output voltage shall be increased for higher RF output power
   */
  LL_PWR_SMPS_SetStartupCurrent(LL_PWR_SMPS_STARTUP_CURRENT_80MA);
  LL_PWR_SMPS_SetOutputVoltageLevel(LL_PWR_SMPS_OUTPUT_VOLTAGE_1V40);
  LL_PWR_SMPS_Enable();
#endif /* CFG_USE_SMPS != 0 */

  return;
}

void Init_Exti(void)
{
  /* Enable IPCC(36), HSEM(38) wakeup interrupts on CPU1 */
  LL_EXTI_EnableIT_32_63(LL_EXTI_LINE_36 | LL_EXTI_LINE_38);

  return;
}

/* USER CODE BEGIN FD */

/* USER CODE END FD */

/*************************************************************
 *
 * LOCAL FUNCTIONS
 *
 *************************************************************/
static void Reset_Device(void)
{
#if (CFG_HW_RESET_BY_FW == 1)
  Reset_BackupDomain();

  Reset_IPCC();
#endif /* CFG_HW_RESET_BY_FW == 1 */

  return;
}

#if (CFG_HW_RESET_BY_FW == 1)
static void Reset_BackupDomain(void)
{
  if ((LL_RCC_IsActiveFlag_PINRST() != FALSE) && (LL_RCC_IsActiveFlag_SFTRST() == FALSE))
  {
    HAL_PWR_EnableBkUpAccess(); /**< Enable access to the RTC registers */

    /**
     *  Write twice the value to flush the APB-AHB bridge
     *  This bit shall be written in the register before writing the next one
     */
    HAL_PWR_EnableBkUpAccess();

    __HAL_RCC_BACKUPRESET_FORCE();
    __HAL_RCC_BACKUPRESET_RELEASE();
  }

  return;
}

static void Reset_IPCC(void)
{
  LL_AHB3_GRP1_EnableClock(LL_AHB3_GRP1_PERIPH_IPCC);

  LL_C1_IPCC_ClearFlag_CHx(
      IPCC,
      LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 | LL_IPCC_CHANNEL_4
      | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);

  LL_C2_IPCC_ClearFlag_CHx(
      IPCC,
      LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 | LL_IPCC_CHANNEL_4
      | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);

  LL_C1_IPCC_DisableTransmitChannel(
      IPCC,
      LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 | LL_IPCC_CHANNEL_4
      | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);

  LL_C2_IPCC_DisableTransmitChannel(
      IPCC,
      LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 | LL_IPCC_CHANNEL_4
      | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);

  LL_C1_IPCC_DisableReceiveChannel(
      IPCC,
      LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 | LL_IPCC_CHANNEL_4
      | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);

  LL_C2_IPCC_DisableReceiveChannel(
      IPCC,
      LL_IPCC_CHANNEL_1 | LL_IPCC_CHANNEL_2 | LL_IPCC_CHANNEL_3 | LL_IPCC_CHANNEL_4
      | LL_IPCC_CHANNEL_5 | LL_IPCC_CHANNEL_6);

  return;
}
#endif /* CFG_HW_RESET_BY_FW == 1 */

static void Config_HSE(void)
{
    OTP_ID0_t * p_otp;

  /**
   * Read HSE_Tuning from OTP
   */
  p_otp = (OTP_ID0_t *) OTP_Read(0);
  if (p_otp)
  {
    LL_RCC_HSE_SetCapacitorTuning(p_otp->hse_tuning);
  }

  return;
}

static void System_Init(void)
{
  Init_Smps();

  Init_Exti();

  Init_Rtc();

  return;
}

static void Init_Rtc(void)
{
  /* Disable RTC registers write protection */
  LL_RTC_DisableWriteProtection(RTC);

  LL_RTC_WAKEUP_SetClock(RTC, CFG_RTC_WUCKSEL_DIVIDER);

  /* Enable RTC registers write protection */
  LL_RTC_EnableWriteProtection(RTC);

  return;
}

/**
 * @brief  Configure the system for power optimization
 *
 * @note  This API configures the system to be ready for low power mode
 *
 * @param  None
 * @retval None
 */
static void SystemPower_Config(void)
{
  /**
   * Select HSI as system clock source after Wake Up from Stop mode
   */
  LL_RCC_SetClkAfterWakeFromStop(LL_RCC_STOP_WAKEUPCLOCK_HSI);

  /* Initialize low power manager */
  UTIL_LPM_Init();
  /* Initialize the CPU2 reset value before starting CPU2 with C2BOOT */
  LL_C2_PWR_SetPowerMode(LL_PWR_MODE_SHUTDOWN);

#if (CFG_USB_INTERFACE_ENABLE != 0)
  /**
   *  Enable USB power
   */
  HAL_PWREx_EnableVddUSB();
#endif /* CFG_USB_INTERFACE_ENABLE != 0 */

  return;
}

static void appe_Tl_Init(void)
{
  TL_MM_Config_t tl_mm_config;
  SHCI_TL_HciInitConf_t SHci_Tl_Init_Conf;
  /**< Reference table initialization */
  TL_Init();

  /**< System channel initialization */
  UTIL_SEQ_RegTask(1<< CFG_TASK_SYSTEM_HCI_ASYNCH_EVT_ID, UTIL_SEQ_RFU, shci_user_evt_proc);
  SHci_Tl_Init_Conf.p_cmdbuffer = (uint8_t*)&SystemCmdBuffer;
  SHci_Tl_Init_Conf.StatusNotCallBack = APPE_SysStatusNot;
  shci_init(APPE_SysUserEvtRx, (void*) &SHci_Tl_Init_Conf);

  /**< Memory Manager channel initialization */
  tl_mm_config.p_BleSpareEvtBuffer = BleSpareEvtBuffer;
  tl_mm_config.p_SystemSpareEvtBuffer = SystemSpareEvtBuffer;
  tl_mm_config.p_AsynchEvtPool = EvtPool;
  tl_mm_config.AsynchEvtPoolSize = POOL_SIZE;
  TL_MM_Init(&tl_mm_config);

  /**
   * If CPU2 was already booted (e.g. after a debugger soft-reset that only
   * resets CPU1), send SHCI_C2_Reinit so CPU2 waits for a new C2BOOT event.
   * TL_Enable() will then re-trigger it properly.
   * On a clean power-on the C2BOOT bit is 0 and this branch is skipped.
   */
  if ((READ_BIT(PWR->CR4, PWR_CR4_C2BOOT)) != 0UL)
  {
    APP_DBG_MSG(">>== CPU2 already booted, sending SHCI_C2_Reinit\n\r");
    SHCI_C2_Reinit();
  }

  TL_Enable();

  return;
}

static void APPE_SysStatusNot(SHCI_TL_CmdStatus_t status)
{
  UNUSED(status);
  return;
}

static const char *APPE_GetStackTypeString(uint8_t stackType)
{
  switch (stackType)
  {
    case INFO_STACK_TYPE_BLE_FULL:
      return "BLE_FULL";
    case INFO_STACK_TYPE_BLE_HCI:
      return "BLE_HCI";
    case INFO_STACK_TYPE_BLE_LIGHT:
      return "BLE_LIGHT";
    case INFO_STACK_TYPE_BLE_BEACON:
      return "BLE_BEACON";
    case INFO_STACK_TYPE_BLE_BASIC:
      return "BLE_BASIC";
    case INFO_STACK_TYPE_BLE_FULL_EXT_ADV:
      return "BLE_FULL_EXT_ADV";
    case INFO_STACK_TYPE_BLE_HCI_EXT_ADV:
      return "BLE_HCI_EXT_ADV";
    case INFO_STACK_TYPE_BLE_THREAD_FTD_STATIC:
      return "BLE_THREAD_FTD_STATIC";
    case INFO_STACK_TYPE_BLE_THREAD_FTD_DYNAMIC:
      return "BLE_THREAD_FTD_DYNAMIC";
    case INFO_STACK_TYPE_BLE_THREAD_LIGHT_DYNAMIC:
      return "BLE_THREAD_LIGHT_DYNAMIC";
    case INFO_STACK_TYPE_BLE_ZIGBEE_FFD_STATIC:
      return "BLE_ZIGBEE_FFD_STATIC";
    case INFO_STACK_TYPE_BLE_ZIGBEE_RFD_STATIC:
      return "BLE_ZIGBEE_RFD_STATIC";
    case INFO_STACK_TYPE_BLE_ZIGBEE_FFD_DYNAMIC:
      return "BLE_ZIGBEE_FFD_DYNAMIC";
    case INFO_STACK_TYPE_BLE_ZIGBEE_RFD_DYNAMIC:
      return "BLE_ZIGBEE_RFD_DYNAMIC";
    case INFO_STACK_TYPE_BLE_MAC_STATIC:
      return "BLE_MAC_STATIC";
    default:
      return "UNKNOWN";
  }
}

/**
 * The type of the payload for a system user event is tSHCI_UserEvtRxParam
 * When the system event is both :
 *    - a ready event (subevtcode = SHCI_SUB_EVT_CODE_READY)
 *    - reported by the FUS (sysevt_ready_rsp == FUS_FW_RUNNING)
 * The buffer shall not be released
 * (eg ((tSHCI_UserEvtRxParam*)pPayload)->status shall be set to SHCI_TL_UserEventFlow_Disable)
 * When the status is not filled, the buffer is released by default
 */
static void APPE_SysUserEvtRx(void * pPayload)
{
  TL_AsynchEvt_t *p_sys_event;
  WirelessFwInfo_t WirelessInfo;

  p_sys_event = (TL_AsynchEvt_t*)(((tSHCI_UserEvtRxParam*)pPayload)->pckt->evtserial.evt.payload);

  switch(p_sys_event->subevtcode)
  {
  case SHCI_SUB_EVT_CODE_READY:
    /* Read the firmware version of both the wireless firmware and the FUS */
    SHCI_GetWirelessFwInfo(&WirelessInfo);
    APP_DBG_MSG("Wireless Firmware version %d.%d.%d\n", WirelessInfo.VersionMajor, WirelessInfo.VersionMinor, WirelessInfo.VersionSub);
    APP_DBG_MSG("Wireless Firmware build %d\n", WirelessInfo.VersionReleaseType);
    APP_DBG_MSG("Wireless Stack type 0x%02x (%s)\n", WirelessInfo.StackType, APPE_GetStackTypeString(WirelessInfo.StackType));
    APP_DBG_MSG("FUS version %d.%d.%d\n", WirelessInfo.FusVersionMajor, WirelessInfo.FusVersionMinor, WirelessInfo.FusVersionSub);

    APP_DBG_MSG(">>== SHCI_SUB_EVT_CODE_READY\n\r");
    APPE_SysEvtReadyProcessing(pPayload);
    break;

  case SHCI_SUB_EVT_ERROR_NOTIF:
    APP_DBG_MSG(">>== SHCI_SUB_EVT_ERROR_NOTIF \n\r");
    APPE_SysEvtError(pPayload);
    break;

  case SHCI_SUB_EVT_BLE_NVM_RAM_UPDATE:
    APP_DBG_MSG(">>== SHCI_SUB_EVT_BLE_NVM_RAM_UPDATE -- BLE NVM RAM HAS BEEN UPDATED BY CPU2 \n");
    APP_DBG_MSG("     - StartAddress = %x , Size = %u\n",
                ((SHCI_C2_BleNvmRamUpdate_Evt_t*)p_sys_event->payload)->StartAddress,
                ((SHCI_C2_BleNvmRamUpdate_Evt_t*)p_sys_event->payload)->Size);
    break;

  case SHCI_SUB_EVT_NVM_START_WRITE:
    APP_DBG_MSG("==>> SHCI_SUB_EVT_NVM_START_WRITE : NumberOfWords = %u\n",
                ((SHCI_C2_NvmStartWrite_Evt_t*)p_sys_event->payload)->NumberOfWords);
    break;

  case SHCI_SUB_EVT_NVM_END_WRITE:
    APP_DBG_MSG(">>== SHCI_SUB_EVT_NVM_END_WRITE\n\r");
    break;

  case SHCI_SUB_EVT_NVM_START_ERASE:
    APP_DBG_MSG("==>>SHCI_SUB_EVT_NVM_START_ERASE : NumberOfSectors = %u\n",
                ((SHCI_C2_NvmStartErase_Evt_t*)p_sys_event->payload)->NumberOfSectors);
    break;

  case SHCI_SUB_EVT_NVM_END_ERASE:
    APP_DBG_MSG(">>== SHCI_SUB_EVT_NVM_END_ERASE\n\r");
    break;

  default:
    break;
  }

  return;
}

/**
 * @brief Notify a system error coming from the M0 firmware
 * @param  ErrorCode  : errorCode detected by the M0 firmware
 *
 * @retval None
 */
static void APPE_SysEvtError(void * pPayload)
{
  TL_AsynchEvt_t *p_sys_event;
  SCHI_SystemErrCode_t *p_sys_error_code;

  p_sys_event = (TL_AsynchEvt_t*)(((tSHCI_UserEvtRxParam*)pPayload)->pckt->evtserial.evt.payload);
  p_sys_error_code = (SCHI_SystemErrCode_t*) p_sys_event->payload;

  APP_DBG_MSG(">>== SHCI_SUB_EVT_ERROR_NOTIF WITH REASON %x \n\r",(*p_sys_error_code));

  if ((*p_sys_error_code) == ERR_BLE_INIT)
  {
    /* Error during BLE stack initialization */
    APP_DBG_MSG(">>== SHCI_SUB_EVT_ERROR_NOTIF WITH REASON - ERR_BLE_INIT \n");
  }
  else
  {
    APP_DBG_MSG(">>== SHCI_SUB_EVT_ERROR_NOTIF WITH REASON - BLE ERROR \n");
  }
  return;
}

static void APPE_SysEvtReadyProcessing(void * pPayload)
{
  TL_AsynchEvt_t *p_sys_event;
  SHCI_C2_Ready_Evt_t *p_sys_ready_event;

  SHCI_C2_CONFIG_Cmd_Param_t config_param = {0};
  uint32_t RevisionID=0;
  uint32_t DeviceID=0;

  p_sys_event = (TL_AsynchEvt_t*)(((tSHCI_UserEvtRxParam*)pPayload)->pckt->evtserial.evt.payload);
  p_sys_ready_event = (SHCI_C2_Ready_Evt_t*) p_sys_event->payload;

  if (p_sys_ready_event->sysevt_ready_rsp == WIRELESS_FW_RUNNING)
  {
    /**
    * The wireless firmware is running on the CPU2
    */
    APP_DBG_MSG(">>== WIRELESS_FW_RUNNING \n");

    /* Traces channel initialization */
    APPD_EnableCPU2();

    /* Enable all events Notification */
    config_param.PayloadCmdSize = SHCI_C2_CONFIG_PAYLOAD_CMD_SIZE;
    config_param.EvtMask1 = SHCI_C2_CONFIG_EVTMASK1_BIT0_ERROR_NOTIF_ENABLE
      +  SHCI_C2_CONFIG_EVTMASK1_BIT1_BLE_NVM_RAM_UPDATE_ENABLE
        +  SHCI_C2_CONFIG_EVTMASK1_BIT2_THREAD_NVM_RAM_UPDATE_ENABLE
          +  SHCI_C2_CONFIG_EVTMASK1_BIT3_NVM_START_WRITE_ENABLE
            +  SHCI_C2_CONFIG_EVTMASK1_BIT4_NVM_END_WRITE_ENABLE
              +  SHCI_C2_CONFIG_EVTMASK1_BIT5_NVM_START_ERASE_ENABLE
                +  SHCI_C2_CONFIG_EVTMASK1_BIT6_NVM_END_ERASE_ENABLE;

    /* Read revision identifier */
    /**
    * @brief  Return the device revision identifier
    * @note   This field indicates the revision of the device.
    * @rmtoll DBGMCU_IDCODE REV_ID        LL_DBGMCU_GetRevisionID
    * @retval Values between Min_Data=0x00 and Max_Data=0xFFFF
    */
    RevisionID = LL_DBGMCU_GetRevisionID();

    APP_DBG_MSG(">>== DBGMCU_GetRevisionID= %x \n\r", RevisionID);

    config_param.RevisionID = (uint16_t)RevisionID;

    DeviceID = LL_DBGMCU_GetDeviceID();
    APP_DBG_MSG(">>== DBGMCU_GetDeviceID= %x \n\r", DeviceID);
    config_param.DeviceID = (uint16_t)DeviceID;
    (void)SHCI_C2_Config(&config_param);

    APP_BLE_Init();
    UTIL_LPM_SetOffMode(1U << CFG_LPM_APP, UTIL_LPM_ENABLE);
  }
  else if (p_sys_ready_event->sysevt_ready_rsp == FUS_FW_RUNNING)
  {
    /**
    * The FUS firmware is running on the CPU2
    * In the scope of this application, there should be no case when we get here
    */
    APP_DBG_MSG(">>== SHCI_SUB_EVT_CODE_READY - FUS_FW_RUNNING \n\r");

    /* The packet shall not be released as this is not supported by the FUS */
    ((tSHCI_UserEvtRxParam*)pPayload)->status = SHCI_TL_UserEventFlow_Disable;
  }
  else
  {
    APP_DBG_MSG(">>== SHCI_SUB_EVT_CODE_READY - UNEXPECTED CASE \n\r");
  }

  return;
}

/* USER CODE BEGIN FD_LOCAL_FUNCTIONS */

/* USER CODE END FD_LOCAL_FUNCTIONS */

/*************************************************************
 *
 * WRAP FUNCTIONS
 *
 *************************************************************/
void HAL_Delay(uint32_t Delay)
{
  uint32_t tickstart = HAL_GetTick();
  uint32_t wait = Delay;

  /* Add a freq to guarantee minimum wait */
  if (wait < HAL_MAX_DELAY)
  {
    wait += HAL_GetTickFreq();
  }

  while ((HAL_GetTick() - tickstart) < wait)
  {
    /************************************************************************************
     * ENTER SLEEP MODE
     ***********************************************************************************/
    LL_LPM_EnableSleep(); /**< Clear SLEEPDEEP bit of Cortex System Control Register */

    /**
     * This option is used to ensure that store operations are completed
     */
  #if defined (__CC_ARM) || defined (__ARMCC_VERSION)
    __force_stores();
  #endif /* __ARMCC_VERSION */

    __WFI();
  }
}

void MX_APPE_Process(void)
{
  /* USER CODE BEGIN MX_APPE_Process_1 */

  /* USER CODE END MX_APPE_Process_1 */
  UTIL_SEQ_Run(UTIL_SEQ_DEFAULT);
  APPE_WatchdogHeartbeat();
  /* USER CODE BEGIN MX_APPE_Process_2 */
  /* USER CODE END MX_APPE_Process_2 */
}

void UTIL_SEQ_Idle(void)
{
  /* An idle sequencer is healthy on CPU1. Refresh immediately before sleep;
   * a task/callback that never returns cannot reach this point. */
  APPE_WatchdogHeartbeat();
#if (CFG_LPM_SUPPORTED == 1)
  UTIL_LPM_EnterLowPower();
#endif /* CFG_LPM_SUPPORTED == 1 */
  return;
}

void APPE_WatchdogHeartbeat(void)
{
  (void)HAL_IWDG_Refresh(&happe_iwdg);
  ModeManager_HealthPoll();
}

static void APPE_IwdgInit(void)
{
  happe_iwdg.Instance = IWDG;
  happe_iwdg.Init.Prescaler = IWDG_PRESCALER_256;
  happe_iwdg.Init.Reload = 4095U;
  happe_iwdg.Init.Window = IWDG_WINDOW_DISABLE;

  if (HAL_IWDG_Init(&happe_iwdg) != HAL_OK)
  {
    NVIC_SystemReset();
  }
}

static void APPE_IwdgWakeup(void)
{
  /* Wakeup-only heartbeat. See registration in MX_APPE_Init(). */
}

/**
  * @brief  This function is called by the scheduler each time an event
  *         is pending.
  *
  * @param  evt_waited_bm : Event pending.
  * @retval None
  */
void UTIL_SEQ_EvtIdle(UTIL_SEQ_bm_t task_id_bm, UTIL_SEQ_bm_t evt_waited_bm)
{
  UTIL_SEQ_bm_t run_mask;

  (void)evt_waited_bm;

  /* While waiting for command responses, only run transport async tasks.
   * This avoids re-entering application/mesh tasks that can issue nested
   * blocking HCI commands and corrupt sequencer context. */
  run_mask = ((UTIL_SEQ_bm_t)1U << CFG_TASK_HCI_ASYNCH_EVT_ID)
           | ((UTIL_SEQ_bm_t)1U << CFG_TASK_SYSTEM_HCI_ASYNCH_EVT_ID);

  UTIL_SEQ_Run((~task_id_bm) & run_mask);
  return;
}

void shci_notify_asynch_evt(void* pdata)
{
  UTIL_SEQ_SetTask(1<<CFG_TASK_SYSTEM_HCI_ASYNCH_EVT_ID, CFG_SCH_PRIO_0);
  return;
}

/* USER CODE BEGIN FD_WRAP_FUNCTIONS */

/* USER CODE END FD_WRAP_FUNCTIONS */
