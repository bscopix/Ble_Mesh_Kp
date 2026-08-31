#include <string.h>

#include "main.h"
#include "flash_driver.h"
#include "mode_manager.h"
#include "nfc_eeprom_mngt.h"
#include "stm32_seq.h"
#include "stm32wbxx_it.h"

#if defined(STM32WB15xx)
#define MODE_NVM_PAGE_SIZE                       2048U
#else
#define MODE_NVM_PAGE_SIZE                       4096U
#endif

#define MODE_NVM_PAGE_COUNT                         2U
#define MODE_NVM_MAGIC                      0x4D4F4445UL /* MODE */
#define MODE_NVM_SCHEMA_VERSION                     1U
#define MODE_NVM_RECORD_SIZE                       40U
#define MODE_NVM_COMMIT_LOW                 0x434F4D4DUL /* COMM */
#define MODE_NVM_COMMIT_HIGH                0x49545445UL /* ITTE */
#define MODE_NVM_ERASED_WORD                0xFFFFFFFFUL
#define MODE_BACKUP_STATE_MAGIC             0x4D4F0000UL
#define MODE_BACKUP_STATE_MASK              0xFFFF0000UL
#define MODE_BACKUP_PLANNED_RESET           0x504C414EUL
#define MODE_BACKUP_FAULT_RESET             0x4641554CUL
#define MODE_CRASH_LOOP_THRESHOLD                    3U
#define MODE_HEALTHY_UPTIME_MS                   60000UL

/* Ten words keep each record naturally aligned for STM32WB double-word writes.
 * Words 8 and 9 are written last and form the commit marker. */
typedef struct
{
  uint32_t magic;
  uint32_t schema_and_size;
  uint32_t generation;
  uint32_t transition_id;
  uint32_t mode_fields;
  uint32_t reset_flags;
  uint32_t reserved;
  uint32_t payload_crc;
  uint32_t commit_low;
  uint32_t commit_high;
} __attribute__((aligned(8))) ModeNvmRecord_t;

extern const void *modeNvmBase;

static ModeNvmRecord_t ModeManager_Record;
static bool ModeManager_RecordValid;
static uint32_t ModeManager_LatestPage;
static BootMode_t ModeManager_ActualMode;
static uint8_t ModeManager_TransitionInProgress;
static uint8_t ModeManager_ConsecutiveUnplannedResets;
static uint8_t ModeManager_RuntimeLastError;
static bool ModeManager_HealthyBootRecorded;

static void ModeManager_ResetTask(void)
{
  /* The response is queued before this task runs. Clients still confirm
   * success by actively checking the destination transport after reboot. */
  HAL_Delay(250U);
  NVIC_SystemReset();
}

static uint32_t ModeManager_Crc32(const void *data, uint32_t length)
{
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFUL;
  uint32_t i;
  uint32_t bit;

  for (i = 0U; i < length; i++)
  {
    crc ^= bytes[i];
    for (bit = 0U; bit < 8U; bit++)
    {
      crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0xEDB88320UL);
    }
  }

  return ~crc;
}

static bool ModeManager_IsGenerationNewer(uint32_t candidate, uint32_t current)
{
  return ((int32_t)(candidate - current) > 0);
}

static bool ModeManager_IsRecordValid(const ModeNvmRecord_t *record)
{
  uint8_t mode;

  if ((record->magic != MODE_NVM_MAGIC) ||
      (record->schema_and_size !=
       (((uint32_t)MODE_NVM_SCHEMA_VERSION << 16) | MODE_NVM_RECORD_SIZE)) ||
      (record->commit_low != MODE_NVM_COMMIT_LOW) ||
      (record->commit_high != MODE_NVM_COMMIT_HIGH))
  {
    return false;
  }

  mode = (uint8_t)(record->mode_fields & 0xFFU);
  if (mode > (uint8_t)BOOT_MODE_GATT_RECOVERY)
  {
    return false;
  }

  return (record->payload_crc == ModeManager_Crc32(record, 28U));
}

static bool ModeManager_IsSlotErased(const ModeNvmRecord_t *record)
{
  return ((record->magic == MODE_NVM_ERASED_WORD) &&
          (record->schema_and_size == MODE_NVM_ERASED_WORD));
}

static void ModeManager_Scan(void)
{
  const uint8_t *base = (const uint8_t *)modeNvmBase;
  uint32_t page;
  uint32_t offset;

  ModeManager_RecordValid = false;
  ModeManager_LatestPage = 0U;
  memset(&ModeManager_Record, 0, sizeof(ModeManager_Record));

  for (page = 0U; page < MODE_NVM_PAGE_COUNT; page++)
  {
    for (offset = 0U;
         (offset + sizeof(ModeNvmRecord_t)) <= MODE_NVM_PAGE_SIZE;
         offset += sizeof(ModeNvmRecord_t))
    {
      const ModeNvmRecord_t *candidate =
          (const ModeNvmRecord_t *)(base + (page * MODE_NVM_PAGE_SIZE) + offset);

      if (ModeManager_IsRecordValid(candidate) &&
          ((!ModeManager_RecordValid) ||
           ModeManager_IsGenerationNewer(candidate->generation,
                                         ModeManager_Record.generation)))
      {
        memcpy(&ModeManager_Record, candidate, sizeof(ModeManager_Record));
        ModeManager_RecordValid = true;
        ModeManager_LatestPage = page;
      }
    }
  }
}

static BootMode_t ModeManager_InferLegacyMode(void)
{
  if (IsProvisioningBootRequested())
  {
    return BOOT_MODE_MESH_PROVISIONING;
  }

  if (GetProvisioningResult() == PROVISION_RESULT_SUCCESS)
  {
    return BOOT_MODE_MESH_OPERATIONAL;
  }

  return BOOT_MODE_LEGACY_GATT;
}

static ModeNvmRecord_t *ModeManager_FindEmptySlot(uint32_t page)
{
  uint8_t *base = (uint8_t *)modeNvmBase + (page * MODE_NVM_PAGE_SIZE);
  uint32_t offset;

  for (offset = 0U;
       (offset + sizeof(ModeNvmRecord_t)) <= MODE_NVM_PAGE_SIZE;
       offset += sizeof(ModeNvmRecord_t))
  {
    ModeNvmRecord_t *candidate = (ModeNvmRecord_t *)(base + offset);
    if (ModeManager_IsSlotErased(candidate))
    {
      return candidate;
    }
  }

  return NULL;
}

static uint32_t ModeManager_PageOfAddress(const void *address)
{
  return ((uint32_t)address - FLASH_BASE) / MODE_NVM_PAGE_SIZE;
}

static bool ModeManager_WriteRecord(ModeNvmRecord_t *record)
{
  ModeNvmRecord_t *destination;
  uint32_t preferred_page;
  uint32_t alternate_page;
  uint64_t commit;

  preferred_page = ModeManager_RecordValid ? ModeManager_LatestPage : 0U;

  destination = ModeManager_FindEmptySlot(preferred_page);
  if (destination == NULL)
  {
    alternate_page = (preferred_page + 1U) % MODE_NVM_PAGE_COUNT;
    if (FD_EraseSectors(ModeManager_PageOfAddress(
                            (const uint8_t *)modeNvmBase +
                            alternate_page * MODE_NVM_PAGE_SIZE),
                        1U) != 0U)
    {
      return false;
    }
    destination = ModeManager_FindEmptySlot(alternate_page);
  }

  if (destination == NULL)
  {
    return false;
  }

  record->commit_low = MODE_NVM_ERASED_WORD;
  record->commit_high = MODE_NVM_ERASED_WORD;
  if (FD_WriteData((uint32_t)destination,
                   (uint64_t *)record,
                   4U) != 0U)
  {
    return false;
  }

  if (memcmp(destination, record, 32U) != 0)
  {
    return false;
  }

  commit = ((uint64_t)MODE_NVM_COMMIT_HIGH << 32) | MODE_NVM_COMMIT_LOW;
  if (FD_WriteData((uint32_t)destination + 32U, &commit, 1U) != 0U)
  {
    return false;
  }

  return ((destination->commit_low == MODE_NVM_COMMIT_LOW) &&
          (destination->commit_high == MODE_NVM_COMMIT_HIGH) &&
          ModeManager_IsRecordValid(destination));
}

void ModeManager_Init(uint32_t reset_flags)
{
  uint32_t backup_state;
  uint8_t backup_mode;
  uint8_t backup_count;
  bool planned_reset;
  bool fault_reset;
  bool unplanned_reset;

  ModeManager_Scan();
  ModeManager_ActualMode = ModeManager_RecordValid
      ? (BootMode_t)(ModeManager_Record.mode_fields & 0xFFU)
      : ModeManager_InferLegacyMode();
  ModeManager_TransitionInProgress = 0U;
  ModeManager_RuntimeLastError = MODE_ERROR_NONE;
  ModeManager_HealthyBootRecorded = false;

  backup_state = RTC->BKP0R;
  backup_mode = (uint8_t)((backup_state >> 8) & 0xFFU);
  backup_count = (uint8_t)(backup_state & 0xFFU);
  if (((backup_state & MODE_BACKUP_STATE_MASK) != MODE_BACKUP_STATE_MAGIC) ||
      (backup_mode != (uint8_t)ModeManager_ActualMode))
  {
    backup_count = 0U;
  }

  planned_reset = (RTC->BKP1R == MODE_BACKUP_PLANNED_RESET);
  fault_reset = ((RTC->BKP2R == MODE_BACKUP_FAULT_RESET) ||
                 (FaultContext_IsPending() != 0U));
  RTC->BKP1R = 0U;
  RTC->BKP2R = 0U;

  unplanned_reset = (((reset_flags & (RCC_CSR_IWDGRSTF | RCC_CSR_WWDGRSTF)) != 0U) ||
                     fault_reset);
  if ((reset_flags & (RCC_CSR_BORRSTF | RCC_CSR_PINRSTF)) != 0U)
  {
    backup_count = 0U;
  }
  else if (unplanned_reset && (!planned_reset) && (backup_count < 0xFFU))
  {
    backup_count++;
  }

  ModeManager_ConsecutiveUnplannedResets = backup_count;
  RTC->BKP0R = MODE_BACKUP_STATE_MAGIC |
               ((uint32_t)ModeManager_ActualMode << 8) |
               backup_count;

  if (backup_count >= MODE_CRASH_LOOP_THRESHOLD)
  {
    ModeManager_ActualMode = BOOT_MODE_GATT_RECOVERY;
    ModeManager_RuntimeLastError = MODE_ERROR_CRASH_LOOP;
  }
}

BootMode_t ModeManager_GetBootMode(void)
{
  return ModeManager_ActualMode;
}

bool ModeManager_IsPersistentStateValid(void)
{
  return ModeManager_RecordValid;
}

bool ModeManager_ShouldStartMesh(void)
{
  return ((ModeManager_ActualMode == BOOT_MODE_MESH_PROVISIONING) ||
          (ModeManager_ActualMode == BOOT_MODE_MESH_OPERATIONAL));
}

bool ModeManager_RequestMode(BootMode_t requested_mode, uint32_t transition_id)
{
  ModeNvmRecord_t record;

  if (requested_mode > BOOT_MODE_GATT_RECOVERY)
  {
    return false;
  }

  memset(&record, 0xFF, sizeof(record));
  record.magic = MODE_NVM_MAGIC;
  record.schema_and_size =
      ((uint32_t)MODE_NVM_SCHEMA_VERSION << 16) | MODE_NVM_RECORD_SIZE;
  record.generation = ModeManager_RecordValid
      ? ModeManager_Record.generation + 1U
      : 1U;
  record.transition_id = transition_id;
  record.mode_fields = (uint32_t)requested_mode;
  record.reset_flags = RCC->CSR;
  record.reserved = 0U;
  record.payload_crc = ModeManager_Crc32(&record, 28U);

  if (!ModeManager_WriteRecord(&record))
  {
    return false;
  }

  ModeManager_Scan();
  ModeManager_TransitionInProgress = 1U;
  RTC->BKP1R = MODE_BACKUP_PLANNED_RESET;
  return true;
}

void ModeManager_SetLastError(ModeError_t error)
{
  if (ModeManager_RecordValid)
  {
    ModeNvmRecord_t record;

    memset(&record, 0xFF, sizeof(record));
    record.magic = MODE_NVM_MAGIC;
    record.schema_and_size =
        ((uint32_t)MODE_NVM_SCHEMA_VERSION << 16) | MODE_NVM_RECORD_SIZE;
    record.generation = ModeManager_Record.generation + 1U;
    record.transition_id = ModeManager_Record.transition_id;
    record.mode_fields = (ModeManager_Record.mode_fields & 0xFFU) |
                         ((uint32_t)error << 8);
    record.reset_flags = RCC->CSR;
    record.reserved = 0U;
    record.payload_crc = ModeManager_Crc32(&record, 28U);
    if (ModeManager_WriteRecord(&record))
    {
      ModeManager_Scan();
    }
  }
}

void ModeManager_GetStatus(ModeStatus_t *status)
{
  if (status == NULL)
  {
    return;
  }

  memset(status, 0, sizeof(*status));
  status->actual_mode = (uint8_t)ModeManager_ActualMode;
  status->requested_mode = ModeManager_RecordValid
      ? (uint8_t)(ModeManager_Record.mode_fields & 0xFFU)
      : (uint8_t)ModeManager_ActualMode;
  status->transition_in_progress = ModeManager_TransitionInProgress;
  status->provision_result = GetProvisioningResult();
  status->last_error = (ModeManager_RuntimeLastError != MODE_ERROR_NONE)
      ? ModeManager_RuntimeLastError
      : (ModeManager_RecordValid
          ? (uint8_t)((ModeManager_Record.mode_fields >> 8) & 0xFFU)
          : MODE_ERROR_NONE);
  status->consecutive_unplanned_resets = ModeManager_ConsecutiveUnplannedResets;
  status->transition_id = ModeManager_RecordValid
      ? ModeManager_Record.transition_id
      : 0U;
}

uint32_t ModeManager_GetCapabilities(void)
{
  return MODE_CAP_GATT_TO_MESH |
         MODE_CAP_MESH_TO_GATT |
         MODE_CAP_MODE_STATUS |
         MODE_CAP_AUTOMATIC_RESET |
         MODE_CAP_FAULT_AUTO_RESET;
}

void ModeManager_RegisterTasks(void)
{
  UTIL_SEQ_RegTask(1UL << CFG_TASK_MODE_RESET_REQ_ID,
                   UTIL_SEQ_RFU,
                   ModeManager_ResetTask);
}

void ModeManager_ScheduleReset(void)
{
  UTIL_SEQ_SetTask(1UL << CFG_TASK_MODE_RESET_REQ_ID, CFG_SCH_PRIO_0);
}

void ModeManager_HealthPoll(void)
{
  if ((!ModeManager_HealthyBootRecorded) &&
      (HAL_GetTick() >= MODE_HEALTHY_UPTIME_MS))
  {
    ModeManager_HealthyBootRecorded = true;
    ModeManager_ConsecutiveUnplannedResets = 0U;
    RTC->BKP0R = MODE_BACKUP_STATE_MAGIC |
                 ((uint32_t)ModeManager_ActualMode << 8);
  }
}
