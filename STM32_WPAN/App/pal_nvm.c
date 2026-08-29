/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    pal_nvm.c
  * @author  MCD Application Team
  * @brief   Flash management for the Controller
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
#include "types.h"
#include "pal_nvm.h"
#include "pal_if.h"  
#include <string.h>

#include "ble.h"
#include "shci.h"
#include "mesh_cfg_usr.h"

#include "flash_driver.h"

#include "mesh_cfg.h"
#include "appli_nvm.h"
#include "app_ble.h"

/* Private define ------------------------------------------------------------*/
#define PAL_NVM_FLASH_RETRY_COUNT  32U
#define PAL_NVM_OPERATION_ERASE    0x01U
#define PAL_NVM_OPERATION_ROTATE   0x02U

/* Private variables ---------------------------------------------------------*/
extern MOBLEUINT8 nvm_operation;
extern MOBLEUINT8 nvm_flash_page;
/* The ST library records the last serialized configuration here when
 * MoblePalBluetoothNvmSave() is deferred by an active Scan/Proxy session. */
extern void const *network_conf;
extern MOBLEUINT16 network_conf_size;

/* Private functions ---------------------------------------------------------*/
static MOBLEUINT32 PalNvmManagedStartAddress(void)
{
  MOBLEUINT32 managed_start = (MOBLEUINT32)NVM_BASE;
  MOBLEUINT32 prv_base = (MOBLEUINT32)PRVN_NVM_BASE_OFFSET;

  if ((prv_base != 0U) && (prv_base < managed_start))
  {
    managed_start = prv_base;
  }

  return managed_start;
}

static MOBLE_RESULT PalNvmValidateRange(MOBLEUINT32 address, MOBLEUINT32 size)
{
  const MOBLEUINT32 managed_start = PalNvmManagedStartAddress();
  const MOBLEUINT32 managed_end = (MOBLEUINT32)NVM_BASE + (MOBLEUINT32)NVM_SIZE;
  const uint64_t access_end = (uint64_t)address + (uint64_t)size;

  if (size == 0U)
  {
    return MOBLE_RESULT_FALSE;
  }

  if (address < managed_start)
  {
    return MOBLE_RESULT_INVALIDARG;
  }

  if (access_end > (uint64_t)managed_end)
  {
    return MOBLE_RESULT_INVALIDARG;
  }

  return MOBLE_RESULT_SUCCESS;
}

/**
  * @brief  Gets the page of a given address
  * @param  Addr: Address of the FLASH Memory
  * @retval The page of a given address
  */
static MOBLEUINT32 GetPage(MOBLEUINT32 Addr)
{
  MOBLEUINT32 page = 0;

  if (Addr < (FLASH_BASE + FLASH_BANK_SIZE))
  {
    /* Bank 1 */
    page = (Addr - FLASH_BASE) / FLASH_PAGE_SIZE;
  }
  else
  {
    /* Bank 2 */
    page = (Addr - (FLASH_BASE + FLASH_BANK_SIZE)) / FLASH_PAGE_SIZE;
  }

  return page;
}

static MOBLEBOOL PalNvmPageIsErased(MOBLEUINT32 address)
{
  MOBLEUINT32 i;
  MOBLEUINT32 const *page = (MOBLEUINT32 const *)address;

  for (i = 0U; i < (FLASH_PAGE_SIZE >> 2); i++)
  {
    if (page[i] != 0xFFFFFFFFUL)
    {
      return MOBLE_FALSE;
    }
  }

  return MOBLE_TRUE;
}

/**
* @brief  returns NVM write protect status
* @param  None
* @retval TRUE if flash is write protected
*/
MOBLEBOOL PalNvmIsWriteProtected(void)
{
    /* All flash is writable */
    return MOBLE_FALSE;
}

/**
* @brief  Read NVM
* @param  address: read start address of nvm
* @param  buf: copy of read content
* @param  size: size of memory to be read
* @param  backup: If read from backup memory
* @retval Result of read operation
*/
MOBLE_RESULT PalNvmRead(MOBLEUINT32 address,
                        void *buf, 
                        MOBLEUINT32 size, 
                        MOBLEBOOL backup)
{
  MOBLE_RESULT result = MOBLE_RESULT_SUCCESS;

  if (buf == NULL)
  {
    TRACE_I(TF_PROVISION,"NVM READ invalid buffer addr=0x%08lx size=%lu\r\n",
            (unsigned long)address, (unsigned long)size);
    return MOBLE_RESULT_INVALIDARG;
  }
  
//  printf("MoblePalNvmRead >>>\r\n");  

  result = PalNvmValidateRange(address, size);
  if (result == MOBLE_RESULT_SUCCESS)
  {
    memcpy(buf, (void *)(address), size);
  }
  else
  {
    TRACE_I(TF_PROVISION,"NVM READ failed addr=0x%08lx size=%lu result=%d\r\n",
            (unsigned long)address, (unsigned long)size, result);
  }
  
//  printf("MoblePalNvmRead <<<\r\n");  
  return result;
}

/**
* @brief  Compare with NVM
* @param  offset: start address of nvm to compare
* @param  buf: copy of content
* @param  size: size of memory to be compared
* @param  comparison: outcome of comparison
* @retval Result
*/
MOBLE_RESULT PalNvmCompare(MOBLEUINT32 address,
                           void const *buf, 
                           MOBLEUINT32 size, 
                           MOBLE_NVM_COMPARE* comparison)
{
  MOBLE_RESULT result = MOBLE_RESULT_SUCCESS;
  MOBLEUINT32 i;
  MOBLEUINT32 first_diff = 0xFFFFFFFFUL;
  MOBLEUINT32 first_flash_word = 0xFFFFFFFFUL;
  MOBLEUINT32 first_requested_word = 0xFFFFFFFFUL;
  MOBLEBOOL compared_range_erased = MOBLE_TRUE;

//  printf("MoblePalNvmCompare >>>\r\n");
  
  if ((comparison == NULL) || (buf == NULL))
  {
    result = MOBLE_RESULT_INVALIDARG;
  }
  else if (PalNvmValidateRange(address, size) != MOBLE_RESULT_SUCCESS)
  {
    result = MOBLE_RESULT_INVALIDARG;
  }
  else if (address & 3)
  {
    result = MOBLE_RESULT_INVALIDARG;
  }
  else if (size & 3)
  {
    result = MOBLE_RESULT_INVALIDARG;
  }
  else
  {
    MOBLEUINT32 word_count = size >> 2;
    MOBLEUINT32 const *src = (MOBLEUINT32 const *)buf;
    MOBLEUINT32 const *dst = (MOBLEUINT32 const *)address;

    *comparison = MOBLE_NVM_COMPARE_EQUAL;

    for (i = 0U; i < word_count; i++)
    {
      if (dst[i] != 0xFFFFFFFFUL)
      {
        compared_range_erased = MOBLE_FALSE;
      }

      if ((src[i] != dst[i]) && (first_diff == 0xFFFFFFFFUL))
      {
        first_diff = i;
        first_flash_word = dst[i];
        first_requested_word = src[i];
      }

      /* Scan the complete range: rewriting the complete NVM image over an
       * already programmed STM32WB flash area is unsafe (including ECC
       * doublewords), even when a particular word would only change 1 to 0. */
    }

    if (first_diff != 0xFFFFFFFFUL)
    {
      *comparison = (compared_range_erased == MOBLE_TRUE)
                    ? MOBLE_NVM_COMPARE_NOT_EQUAL
                    : MOBLE_NVM_COMPARE_NOT_EQUAL_ERASE;
    }

    TRACE_I(TF_PROVISION,
            "NVM COMPARE addr=0x%08lx size=%lu cmp=%u diff_word=%lu erased=%u op=%u page=%u flash=%08lx requested=%08lx\r\n",
            (unsigned long)address,
            (unsigned long)size,
            (unsigned int)*comparison,
            (unsigned long)first_diff,
            (unsigned int)compared_range_erased,
            (unsigned int)nvm_operation,
            (unsigned int)nvm_flash_page,
            (unsigned long)first_flash_word,
            (unsigned long)first_requested_word);
  }

  if (result != MOBLE_RESULT_SUCCESS)
  {
    TRACE_I(TF_PROVISION,
            "NVM COMPARE failed addr=0x%08lx size=%lu result=%d op=%u page=%u\r\n",
            (unsigned long)address,
            (unsigned long)size,
            result,
            (unsigned int)nvm_operation,
            (unsigned int)nvm_flash_page);
  }
  
//  printf("MoblePalNvmCompare <<<\r\n");
  return result;
}

/**
* @brief  Erase NVM
* @param  None
* @retval Result
*/
MOBLE_RESULT PalNvmErase(MOBLEUINT32 address,
                         MOBLEUINT8 nb_pages)
{
  MOBLEUINT32 erase_size = (MOBLEUINT32)nb_pages * FLASH_PAGE_SIZE;
  MOBLEUINT32 first_page;
  MOBLEUINT32 pages_left;
  MOBLEUINT32 retry_count = 0U;

  if (nb_pages == 0U)
  {
    TRACE_I(TF_PROVISION,"NVM ERASE invalid addr=0x%08lx pages=0\r\n",
            (unsigned long)address);
    return MOBLE_RESULT_INVALIDARG;
  }

  if (PalNvmValidateRange(address, erase_size) != MOBLE_RESULT_SUCCESS)
  {
    TRACE_I(TF_PROVISION,"NVM ERASE range error addr=0x%08lx pages=%u\r\n",
            (unsigned long)address, nb_pages);
    return MOBLE_RESULT_INVALIDARG;
  }

  first_page = GetPage(address);
  pages_left = nb_pages;

  while ((pages_left != 0U) && (retry_count < PAL_NVM_FLASH_RETRY_COUNT))
  {
    MOBLEUINT32 remaining = FD_EraseSectors(first_page, pages_left);
    MOBLEUINT32 completed = pages_left - remaining;

    first_page += completed;
    pages_left = remaining;
    retry_count++;
  }

  if (pages_left != 0U)
  {
    TRACE_I(TF_PROVISION,"NVM ERASE failed addr=0x%08lx pages=%u remaining=%lu\r\n",
            (unsigned long)address, nb_pages, (unsigned long)pages_left);
    return MOBLE_RESULT_FAIL;
  }

  TRACE_I(TF_PROVISION,"NVM ERASE ok addr=0x%08lx pages=%u\r\n",
          (unsigned long)address, nb_pages);
  return MOBLE_RESULT_SUCCESS;
}

/**
* @brief  Write to NVM
* @param  offset: wrt start address of nvm
* @param  buf: copy of write content
* @param  size: size of memory to be written
* @retval Result
*/
MOBLE_RESULT PalNvmWrite(MOBLEUINT32 address,
                          void const *buf, 
                          MOBLEUINT32 size)
{
  MOBLE_RESULT result = MOBLE_RESULT_SUCCESS;
  MOBLEUINT32 nb_dword = 0U;
  MOBLEUINT32 programmed_size;

  if (buf == NULL)
  {
    result = MOBLE_RESULT_INVALIDARG;
  }
  else if (PalNvmValidateRange(address, size) != MOBLE_RESULT_SUCCESS)
  {
    result = MOBLE_RESULT_INVALIDARG;
  }
  else if (address & 3)
  {
    result = MOBLE_RESULT_INVALIDARG;
  }
  else if (size & 3)
  {
    result = MOBLE_RESULT_INVALIDARG;
  }
  else
  {
    /* Compatibility requirement of libBle_Mesh_CM4_Keil.lib: the serialized
     * configuration size can be 4 mod 8 (3660 bytes with v1.13.011), while
     * the backing object contains the final padded word.  The ST reference
     * PAL rounds the transfer up and the library expects those 4 bytes to be
     * present when validating the configuration after reboot. */
    nb_dword = (size + 7U) >> 3;
    programmed_size = nb_dword << 3;

    if (FD_WriteData(address, (uint64_t *)buf, nb_dword) != 0U)
    {
      result = MOBLE_RESULT_FAIL;
    }
  }

  if (result == MOBLE_RESULT_SUCCESS)
  {
    TRACE_I(TF_PROVISION,"NVM WRITE ok addr=0x%08lx size=%lu programmed=%lu\r\n",
            (unsigned long)address, (unsigned long)size,
            (unsigned long)programmed_size);
  }
  else
  {
    TRACE_I(TF_PROVISION,"NVM WRITE failed addr=0x%08lx size=%lu result=%d\r\n",
            (unsigned long)address, (unsigned long)size, result);
  }

  return result;
}

#if 0
/**
* @brief  Backup process
* @param  None
* @retval Result
*/
static MOBLE_RESULT PalNvmBackupProcess(void)
{
  MOBLEUINT32 buff[4*N_BYTES_WORD];
  static MOBLEUINT8 backup_pages_to_be_erased = 0;    
  MOBLE_RESULT result = MOBLE_RESULT_SUCCESS;
    
  if (backup_pages_to_be_erased == 0)
  {
    backup_pages_to_be_erased = 1;
  }
      
  if(backup_pages_to_be_erased != 0)
  {
#if 0
    BLEMesh_StopAdvScan();
    ATOMIC_SECTION_BEGIN();
    if(BluenrgMesh_IsFlashReadyToErase())
    {
      FLASH_ErasePage((uint16_t)((BNRGM_NVM_BACKUP_BASE - 
                                  RESET_MANAGER_FLASH_BASE_ADDRESS) / PAGE_SIZE +
                       BNRGM_NVM_BACKUP_SIZE/PAGE_SIZE - 
                       backup_pages_to_be_erased));
         
      if (FLASH_GetFlagStatus(Flash_CMDERR) == SET)
      {
        result = MOBLE_RESULT_FAIL;
      }
      else
      {
        backup_pages_to_be_erased--;
      }
    }
    else
    {
      /* do nothing */
    }
    ATOMIC_SECTION_END();
#else
    result = PalNvmErase(NVM_BASE, FLASH_SECTOR_SIZE);
    if(result == MOBLE_RESULT_SUCCESS)
      backup_pages_to_be_erased = 0;
#endif
  }
    
  if (result == MOBLE_RESULT_SUCCESS && backup_pages_to_be_erased == 0)
  {
#if 0
    BLEMesh_StopAdvScan();
    ATOMIC_SECTION_BEGIN();
    if(BluenrgMesh_IsFlashReadyToErase())
    {
      for (size_t i = 0; i < BNRGM_NVM_BACKUP_SIZE && FLASH_GetFlagStatus(Flash_CMDERR) == RESET; )
      {
        memcpy((MOBLEUINT8*)buff, (void *)(BNRGM_NVM_BASE + i), 4*N_BYTES_WORD);
        FLASH_ProgramWordBurst(BNRGM_NVM_BACKUP_BASE + i, (uint32_t*)buff);
        i += 4*N_BYTES_WORD;
      }
          
      if (FLASH_GetFlagStatus(Flash_CMDERR) == SET)
      {
        result = MOBLE_RESULT_FAIL;
      }
      else
      {
        PalNvmReqs.backup_req = MOBLE_FALSE;
      }
    }
    else
    {
      /* do nothing */
    }
    ATOMIC_SECTION_END();
#else
#endif
  }
 return result;
}
#endif

/**
* @brief  NVM process
* @param  None
* @retval Result
*/
MOBLE_RESULT PalNvmProcess(void)
{
  MOBLE_RESULT result;
  MOBLEUINT8 operation = nvm_operation;
  MOBLEUINT8 active_page;
  MOBLEUINT8 target_page;
  MOBLEUINT32 active_address;
  MOBLEUINT32 target_address;
  APP_BLE_ConnStatus_t connection_status;
  static MOBLEUINT8 deferred_operation = 0U;

  if (operation == 0U)
  {
    deferred_operation = 0U;
    return MOBLE_RESULT_SUCCESS;
  }

  /* Config Server callbacks run while PB-GATT/Proxy traffic is active.  Wait
   * for disconnection so the serialized RAM image is stable and all commands
   * of one configuration session are committed in a single flash rotation. */
  connection_status = APP_BLE_Get_Server_Connection_Status();
  if ((connection_status == APP_BLE_CONNECTED_SERVER) ||
      (connection_status == APP_BLE_CONNECTED_CLIENT))
  {
    if (deferred_operation != operation)
    {
      TRACE_I(TF_PROVISION,
              "NVM PROCESS deferred op=%u page=%u connection=%u\r\n",
              (unsigned int)operation,
              (unsigned int)nvm_flash_page,
              (unsigned int)connection_status);
      deferred_operation = operation;
    }
    return MOBLE_RESULT_SUCCESS;
  }

  deferred_operation = 0U;
  active_page = (MOBLEUINT8)(nvm_flash_page & 0x01U);
  active_address = (MOBLEUINT32)NVM_BASE +
                   ((MOBLEUINT32)active_page * FLASH_PAGE_SIZE);

  if ((operation & PAL_NVM_OPERATION_ROTATE) != 0U)
  {
    if ((network_conf == NULL) || (network_conf_size == 0U) ||
        (((MOBLEUINT32)network_conf_size + 7U) > FLASH_PAGE_SIZE))
    {
      TRACE_I(TF_PROVISION,
              "NVM PROCESS invalid image op=%u ptr=0x%08lx size=%u\r\n",
              (unsigned int)operation,
              (unsigned long)network_conf,
              (unsigned int)network_conf_size);
      return MOBLE_RESULT_INVALIDARG;
    }

    target_page = (MOBLEUINT8)(active_page ^ 0x01U);
    target_address = (MOBLEUINT32)NVM_BASE +
                     ((MOBLEUINT32)target_page * FLASH_PAGE_SIZE);

    TRACE_I(TF_PROVISION,
            "NVM PROCESS rotate begin op=%u active=%u target=%u size=%u\r\n",
            (unsigned int)operation,
            (unsigned int)active_page,
            (unsigned int)target_page,
            (unsigned int)network_conf_size);

    /* Normally the inactive page was erased after the previous commit.  If a
     * reset interrupted cleanup, restore that invariant before programming. */
    if (PalNvmPageIsErased(target_address) == MOBLE_FALSE)
    {
      result = PalNvmErase(target_address, 1U);
      if (result != MOBLE_RESULT_SUCCESS)
      {
        TRACE_I(TF_PROVISION,
                "NVM PROCESS target erase failed page=%u result=%d\r\n",
                (unsigned int)target_page, result);
        return result;
      }
    }

    result = PalNvmWrite(target_address, network_conf, network_conf_size);
    if (result != MOBLE_RESULT_SUCCESS)
    {
      return result;
    }

    if (memcmp((void const *)target_address, network_conf,
               network_conf_size) != 0)
    {
      TRACE_I(TF_PROVISION,
              "NVM PROCESS verify failed target=%u addr=0x%08lx\r\n",
              (unsigned int)target_page,
              (unsigned long)target_address);
      (void)PalNvmErase(target_address, 1U);
      return MOBLE_RESULT_FAIL;
    }

    /* Keep the old valid copy until the new one has been fully verified. */
    result = PalNvmErase(active_address, 1U);
    if (result != MOBLE_RESULT_SUCCESS)
    {
      TRACE_I(TF_PROVISION,
              "NVM PROCESS old erase failed active=%u; retaining op=%u\r\n",
              (unsigned int)active_page,
              (unsigned int)nvm_operation);
      return result;
    }

    nvm_flash_page = target_page;
    nvm_operation = 0U;
    TRACE_I(TF_PROVISION,
            "NVM PROCESS rotate committed active=%u op=%u\r\n",
            (unsigned int)nvm_flash_page,
            (unsigned int)nvm_operation);
    return MOBLE_RESULT_SUCCESS;
  }

  if ((operation & PAL_NVM_OPERATION_ERASE) != 0U)
  {
    TRACE_I(TF_PROVISION,
            "NVM PROCESS erase active=%u op=%u\r\n",
            (unsigned int)active_page,
            (unsigned int)operation);
    result = PalNvmErase(active_address, 1U);
    if (result == MOBLE_RESULT_SUCCESS)
    {
      nvm_operation = 0U;
    }
    return result;
  }

  TRACE_I(TF_PROVISION,"NVM PROCESS unknown op=%u\r\n",
          (unsigned int)operation);
  return MOBLE_RESULT_INVALIDARG;
}

