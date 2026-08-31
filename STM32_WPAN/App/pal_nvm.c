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
#include "appli_mesh.h"
#include "app_ble.h"

/* Private define ------------------------------------------------------------*/
#define PAL_NVM_FLASH_RETRY_COUNT  32U
#define PAL_NVM_OPERATION_ERASE    0x01U
#define PAL_NVM_OPERATION_ROTATE   0x02U
/* Runtime Mesh state (principally RPL and the already block-reserved SEQ)
 * shares the same serialized image as Config Server state.  Coalesce it so
 * sustained traffic causes at most one full-page rotation per interval. */
#define PAL_NVM_RUNTIME_BATCH_MS    (15UL * 60UL * 1000UL)
#define PAL_RUNTIME_DATA_MAGIC      0x4A524E31UL
#define PAL_RUNTIME_TX_MAGIC        0x4A545831UL
#define PAL_RUNTIME_JOURNAL_COMMIT  0x434D5431UL
#define PAL_RUNTIME_RECORD_SIZE     32U
#define PAL_RUNTIME_DELTA_SIZE      8U

typedef struct
{
  uint32_t magic;
  uint16_t offset;
  uint16_t length;
  uint32_t base_crc;
  uint32_t reserved;
  uint64_t data;
  uint32_t record_crc;
  uint32_t commit;
} __attribute__((packed, aligned(8))) PalRuntimeRecord_t;

typedef char PalRuntimeRecordSizeMustBe32[
    (sizeof(PalRuntimeRecord_t) == PAL_RUNTIME_RECORD_SIZE) ? 1 : -1];

/* Private variables ---------------------------------------------------------*/
extern MOBLEUINT8 nvm_operation;
extern MOBLEUINT8 nvm_flash_page;
/* The ST library records the last serialized configuration here when
 * MoblePalBluetoothNvmSave() is deferred by an active Scan/Proxy session. */
extern void const *network_conf;
extern MOBLEUINT16 network_conf_size;
static uint8_t runtime_effective_image[FLASH_PAGE_SIZE] __attribute__((aligned(8)));

/* Private functions ---------------------------------------------------------*/
static uint32_t PalNvmCrc32(void const *data, uint32_t length)
{
  uint8_t const *bytes = (uint8_t const *)data;
  uint32_t crc = 0xFFFFFFFFUL;
  uint32_t i;
  uint32_t bit;

  for (i = 0U; i < length; i++)
  {
    crc ^= bytes[i];
    for (bit = 0U; bit < 8U; bit++)
    {
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
  }
  return ~crc;
}

static MOBLEBOOL PalRuntimeRecordIsErased(PalRuntimeRecord_t const *record)
{
  uint32_t const *words = (uint32_t const *)record;
  uint32_t i;

  for (i = 0U; i < (sizeof(*record) / sizeof(uint32_t)); i++)
  {
    if (words[i] != 0xFFFFFFFFUL)
    {
      return MOBLE_FALSE;
    }
  }
  return MOBLE_TRUE;
}

static MOBLEBOOL PalRuntimeRecordIsValid(PalRuntimeRecord_t const *record)
{
  MOBLEBOOL shape_valid = MOBLE_FALSE;

  if ((record->magic == PAL_RUNTIME_DATA_MAGIC) &&
      (record->length == PAL_RUNTIME_DELTA_SIZE) &&
      ((record->offset & (PAL_RUNTIME_DELTA_SIZE - 1U)) == 0U) &&
      (((uint32_t)record->offset + record->length) <= FLASH_PAGE_SIZE))
  {
    shape_valid = MOBLE_TRUE;
  }
  else if ((record->magic == PAL_RUNTIME_TX_MAGIC) &&
           (record->length == 0U))
  {
    shape_valid = MOBLE_TRUE;
  }

  return ((shape_valid != MOBLE_FALSE) &&
          (record->commit == PAL_RUNTIME_JOURNAL_COMMIT) &&
          (record->record_crc == PalNvmCrc32(record, 24U)))
             ? MOBLE_TRUE
             : MOBLE_FALSE;
}

static MOBLEBOOL PalRuntimeTransactionIsCommitted(
    PalRuntimeRecord_t const *records,
    uint32_t record_count,
    uint32_t data_index,
    uint32_t base_crc,
    uint32_t transaction_id)
{
  uint32_t i;

  for (i = data_index + 1U; i < record_count; i++)
  {
    if ((PalRuntimeRecordIsValid(&records[i]) != MOBLE_FALSE) &&
        (records[i].magic == PAL_RUNTIME_TX_MAGIC) &&
        (records[i].base_crc == base_crc) &&
        (records[i].reserved == transaction_id))
    {
      return MOBLE_TRUE;
    }
  }
  return MOBLE_FALSE;
}

static MOBLE_RESULT PalRuntimeWriteRecord(uint32_t index,
                                          PalRuntimeRecord_t *record)
{
  uint64_t final_doubleword;
  MOBLEUINT32 destination = (MOBLEUINT32)runtimeNvmBase +
                            (index * sizeof(PalRuntimeRecord_t));

  record->record_crc = PalNvmCrc32(record, 24U);
  record->commit = PAL_RUNTIME_JOURNAL_COMMIT;
  if (FD_WriteData(destination, (uint64_t *)record, 3U) != 0U)
  {
    return MOBLE_RESULT_FAIL;
  }
  memcpy(&final_doubleword, &record->record_crc, sizeof(final_doubleword));
  if (FD_WriteData(destination + 24U, &final_doubleword, 1U) != 0U)
  {
    return MOBLE_RESULT_FAIL;
  }
  return (memcmp((void const *)destination, record, sizeof(*record)) == 0)
             ? MOBLE_RESULT_SUCCESS
             : MOBLE_RESULT_FAIL;
}

static MOBLEBOOL PalRuntimeAddressIsMeshPage(MOBLEUINT32 address,
                                             MOBLEUINT32 size,
                                             MOBLEUINT32 *page_address)
{
  MOBLEUINT32 mesh_start = (MOBLEUINT32)NVM_BASE;
  MOBLEUINT32 mesh_end = mesh_start + (MOBLEUINT32)NVM_SIZE;
  MOBLEUINT32 candidate;

  if ((address < mesh_start) || ((uint64_t)address + size > mesh_end))
  {
    return MOBLE_FALSE;
  }

  candidate = mesh_start +
              (((address - mesh_start) / FLASH_PAGE_SIZE) * FLASH_PAGE_SIZE);
  if (((uint64_t)address + size) >
      ((uint64_t)candidate + FLASH_PAGE_SIZE))
  {
    return MOBLE_FALSE;
  }

  *page_address = candidate;
  return MOBLE_TRUE;
}

static void PalRuntimeOverlay(MOBLEUINT32 address, void *buf, MOBLEUINT32 size)
{
  PalRuntimeRecord_t const *records =
      (PalRuntimeRecord_t const *)runtimeNvmBase;
  MOBLEUINT32 page_address;
  uint32_t base_crc;
  uint32_t record_count = FLASH_PAGE_SIZE / sizeof(PalRuntimeRecord_t);
  uint32_t i;

  if ((runtimeNvmBase == NULL) ||
      (PalRuntimeAddressIsMeshPage(address, size, &page_address) == MOBLE_FALSE))
  {
    return;
  }

  base_crc = PalNvmCrc32((void const *)page_address, FLASH_PAGE_SIZE);
  for (i = 0U; i < record_count; i++)
  {
    uint32_t record_start;
    uint32_t record_end;
    uint32_t read_start = address - page_address;
    uint32_t read_end = read_start + size;

    if ((PalRuntimeRecordIsValid(&records[i]) == MOBLE_FALSE) ||
        (records[i].magic != PAL_RUNTIME_DATA_MAGIC) ||
        (records[i].base_crc != base_crc) ||
        (PalRuntimeTransactionIsCommitted(records, record_count, i,
                                          base_crc,
                                          records[i].reserved) == MOBLE_FALSE))
    {
      continue;
    }

    record_start = records[i].offset;
    record_end = record_start + records[i].length;
    if ((record_start < read_end) && (record_end > read_start))
    {
      uint32_t copy_start = (record_start > read_start) ? record_start : read_start;
      uint32_t copy_end = (record_end < read_end) ? record_end : read_end;
      memcpy((uint8_t *)buf + (copy_start - read_start),
             (uint8_t const *)&records[i].data + (copy_start - record_start),
             copy_end - copy_start);
    }
  }
}

static MOBLE_RESULT PalRuntimeReadEffective(MOBLEUINT32 address,
                                            void *buf,
                                            MOBLEUINT32 size)
{
  memcpy(buf, (void const *)address, size);
  PalRuntimeOverlay(address, buf, size);
  return MOBLE_RESULT_SUCCESS;
}

static MOBLE_RESULT PalRuntimeJournalAppend(MOBLEUINT32 active_address,
                                             void const *image,
                                             MOBLEUINT32 image_size)
{
  PalRuntimeRecord_t const *flash_records =
      (PalRuntimeRecord_t const *)runtimeNvmBase;
  PalRuntimeRecord_t record;
  uint32_t record_count = FLASH_PAGE_SIZE / sizeof(PalRuntimeRecord_t);
  uint32_t free_count = 0U;
  uint32_t changed_count = 0U;
  uint32_t first_free = record_count;
  uint32_t rounded_size = (image_size + 7U) & ~7U;
  uint32_t base_crc;
  uint32_t transaction_id = 1U;
  uint32_t offset;
  uint32_t i;

  if ((runtimeNvmBase == NULL) || (image == NULL) ||
      (rounded_size > FLASH_PAGE_SIZE))
  {
    return MOBLE_RESULT_INVALIDARG;
  }

  PalRuntimeReadEffective(active_address, runtime_effective_image, rounded_size);
  for (offset = 0U; offset < rounded_size; offset += PAL_RUNTIME_DELTA_SIZE)
  {
    if (memcmp(&runtime_effective_image[offset],
               (uint8_t const *)image + offset,
               PAL_RUNTIME_DELTA_SIZE) != 0)
    {
      changed_count++;
    }
  }

  if (changed_count == 0U)
  {
    return MOBLE_RESULT_SUCCESS;
  }

  for (i = 0U; i < record_count; i++)
  {
    if (PalRuntimeRecordIsErased(&flash_records[i]) != MOBLE_FALSE)
    {
      if (first_free == record_count)
      {
        first_free = i;
      }
      free_count++;
    }
  }

  /* Append order must remain monotonic. A torn record consumes its slot, so
   * only the erased suffix following the first free slot is usable. */
  for (i = first_free; i < record_count; i++)
  {
    if (PalRuntimeRecordIsErased(&flash_records[i]) == MOBLE_FALSE)
    {
      free_count = i - first_free;
      break;
    }
  }

  if ((first_free == record_count) || (free_count < (changed_count + 1U)))
  {
    return MOBLE_RESULT_OUTOFMEMORY;
  }

  base_crc = PalNvmCrc32((void const *)active_address, FLASH_PAGE_SIZE);
  for (offset = 0U; offset < first_free; offset++)
  {
    if ((PalRuntimeRecordIsValid(&flash_records[offset]) != MOBLE_FALSE) &&
        (flash_records[offset].base_crc == base_crc) &&
        (flash_records[offset].reserved >= transaction_id))
    {
      transaction_id = flash_records[offset].reserved + 1U;
    }
  }
  i = first_free;
  for (offset = 0U; offset < rounded_size; offset += PAL_RUNTIME_DELTA_SIZE)
  {
    if (memcmp(&runtime_effective_image[offset],
               (uint8_t const *)image + offset,
               PAL_RUNTIME_DELTA_SIZE) == 0)
    {
      continue;
    }

    memset(&record, 0, sizeof(record));
    record.magic = PAL_RUNTIME_DATA_MAGIC;
    record.offset = (uint16_t)offset;
    record.length = PAL_RUNTIME_DELTA_SIZE;
    record.base_crc = base_crc;
    record.reserved = transaction_id;
    memcpy(&record.data, (uint8_t const *)image + offset,
           PAL_RUNTIME_DELTA_SIZE);
    if (PalRuntimeWriteRecord(i, &record) != MOBLE_RESULT_SUCCESS)
    {
      return MOBLE_RESULT_FAIL;
    }
    i++;
  }

  /* Commit the complete group last. A reset before this footer leaves valid
   * data records in Flash, but PalRuntimeOverlay deliberately ignores them. */
  memset(&record, 0, sizeof(record));
  record.magic = PAL_RUNTIME_TX_MAGIC;
  record.offset = (uint16_t)changed_count;
  record.length = 0U;
  record.base_crc = base_crc;
  record.reserved = transaction_id;
  if (PalRuntimeWriteRecord(i, &record) != MOBLE_RESULT_SUCCESS)
  {
    return MOBLE_RESULT_FAIL;
  }
  i++;

  TRACE_I(TF_PROVISION,
          "NVM runtime journal appended records=%lu used=%lu/%lu\r\n",
          (unsigned long)changed_count,
          (unsigned long)i,
          (unsigned long)record_count);
  return MOBLE_RESULT_SUCCESS;
}

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
    result = PalRuntimeReadEffective(address, buf, size);
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
    MOBLEUINT32 const *dst;

    if (size > sizeof(runtime_effective_image))
    {
      return MOBLE_RESULT_INVALIDARG;
    }
    PalRuntimeReadEffective(address, runtime_effective_image, size);
    dst = (MOBLEUINT32 const *)runtime_effective_image;

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

MOBLE_RESULT PalNvmRuntimeJournalErase(void)
{
  MOBLEUINT32 first_page;
  MOBLEUINT32 remaining = 1U;
  MOBLEUINT32 retry_count = 0U;

  if (runtimeNvmBase == NULL)
  {
    return MOBLE_RESULT_INVALIDARG;
  }

  if (PalNvmPageIsErased((MOBLEUINT32)runtimeNvmBase) != MOBLE_FALSE)
  {
    return MOBLE_RESULT_SUCCESS;
  }

  first_page = GetPage((MOBLEUINT32)runtimeNvmBase);
  while ((remaining != 0U) && (retry_count < PAL_NVM_FLASH_RETRY_COUNT))
  {
    remaining = FD_EraseSectors(first_page, 1U);
    retry_count++;
  }

  if (remaining != 0U)
  {
    TRACE_I(TF_PROVISION,"NVM runtime journal erase failed addr=0x%08lx\r\n",
            (unsigned long)runtimeNvmBase);
    return MOBLE_RESULT_FAIL;
  }

  TRACE_I(TF_PROVISION,"NVM runtime journal erased\r\n");
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
  static MOBLEBOOL runtime_batch_active = MOBLE_FALSE;
  static MOBLEBOOL runtime_batch_logged = MOBLE_FALSE;
  static uint32_t runtime_batch_started = 0U;
  uint32_t now;
  MOBLEBOOL urgent_commit;
  MOBLEBOOL runtime_compaction = MOBLE_FALSE;

  if (operation == 0U)
  {
    deferred_operation = 0U;
    runtime_batch_active = MOBLE_FALSE;
    runtime_batch_logged = MOBLE_FALSE;
    return MOBLE_RESULT_SUCCESS;
  }

  now = HAL_GetTick();
  urgent_commit = AppliMesh_IsNvmCommitUrgent();

  if ((urgent_commit == MOBLE_FALSE) &&
      (runtime_batch_active == MOBLE_FALSE))
  {
    runtime_batch_active = MOBLE_TRUE;
    runtime_batch_started = now;
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

  /* The ST library uses the same operation bits for Config Server, RPL, SEQ
   * and model state.  Only the application callback can classify a Config
   * Server transaction as urgent.  Everything else remains in RAM and the
   * latest serialized image is committed once per batch interval. */
  if ((urgent_commit == MOBLE_FALSE) &&
      ((uint32_t)(now - runtime_batch_started) < PAL_NVM_RUNTIME_BATCH_MS))
  {
    if (runtime_batch_logged == MOBLE_FALSE)
    {
      TRACE_I(TF_PROVISION,
              "NVM runtime batch pending op=%u interval_ms=%lu\r\n",
              (unsigned int)operation,
              (unsigned long)PAL_NVM_RUNTIME_BATCH_MS);
      runtime_batch_logged = MOBLE_TRUE;
    }
    return MOBLE_RESULT_SUCCESS;
  }

  if (urgent_commit == MOBLE_FALSE)
  {
    TRACE_I(TF_PROVISION,"NVM runtime batch commit op=%u\r\n",
            (unsigned int)operation);
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

    if (urgent_commit == MOBLE_FALSE)
    {
      result = PalRuntimeJournalAppend(active_address, network_conf,
                                       network_conf_size);
      if (result == MOBLE_RESULT_SUCCESS)
      {
        nvm_operation = 0U;
        runtime_batch_active = MOBLE_FALSE;
        runtime_batch_logged = MOBLE_FALSE;
        return MOBLE_RESULT_SUCCESS;
      }
      if (result != MOBLE_RESULT_OUTOFMEMORY)
      {
        TRACE_I(TF_PROVISION,
                "NVM runtime journal append failed result=%d\r\n", result);
        return result;
      }

      /* The append page is full. Atomically consolidate the effective RAM
       * image into the normal double-page store, then reclaim the journal. */
      runtime_compaction = MOBLE_TRUE;
      TRACE_I(TF_PROVISION,"NVM runtime journal full: compacting image\r\n");
    }

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
    runtime_batch_active = MOBLE_FALSE;
    runtime_batch_logged = MOBLE_FALSE;
    if ((urgent_commit != MOBLE_FALSE) ||
        (runtime_compaction != MOBLE_FALSE))
    {
      /* Records contain the CRC of their base page and are already ignored
       * after this rotation. Erase only after the new base was verified. */
      (void)PalNvmRuntimeJournalErase();
    }
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
      runtime_batch_active = MOBLE_FALSE;
      runtime_batch_logged = MOBLE_FALSE;
    }
    return result;
  }

  TRACE_I(TF_PROVISION,"NVM PROCESS unknown op=%u\r\n",
          (unsigned int)operation);
  return MOBLE_RESULT_INVALIDARG;
}

