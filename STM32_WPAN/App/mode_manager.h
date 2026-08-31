#ifndef MODE_MANAGER_H
#define MODE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
  BOOT_MODE_LEGACY_GATT = 0U,
  BOOT_MODE_MESH_PROVISIONING = 1U,
  BOOT_MODE_MESH_OPERATIONAL = 2U,
  BOOT_MODE_GATT_MAINTENANCE = 3U,
  BOOT_MODE_GATT_RECOVERY = 4U
} BootMode_t;

typedef enum
{
  MODE_ERROR_NONE = 0U,
  MODE_ERROR_NVM_WRITE = 1U,
  MODE_ERROR_INVALID_MESH_NVM = 2U,
  MODE_ERROR_CRASH_LOOP = 3U,
  MODE_ERROR_INVALID_REQUEST = 4U
} ModeError_t;

#define MODE_PROTOCOL_VERSION                         1U

#define MODE_CAP_GATT_TO_MESH                 (1UL << 0)
#define MODE_CAP_MESH_TO_GATT                 (1UL << 1)
#define MODE_CAP_MODE_STATUS                  (1UL << 2)
#define MODE_CAP_AUTOMATIC_RESET              (1UL << 3)
#define MODE_CAP_FAULT_AUTO_RESET              (1UL << 4)

typedef struct
{
  uint8_t actual_mode;
  uint8_t requested_mode;
  uint8_t transition_in_progress;
  uint8_t provision_result;
  uint8_t last_error;
  uint8_t consecutive_unplanned_resets;
  uint16_t reserved;
  uint32_t transition_id;
} ModeStatus_t;

void ModeManager_Init(uint32_t reset_flags);
BootMode_t ModeManager_GetBootMode(void);
bool ModeManager_IsPersistentStateValid(void);
bool ModeManager_ShouldStartMesh(void);
bool ModeManager_RequestMode(BootMode_t requested_mode, uint32_t transition_id);
void ModeManager_SetLastError(ModeError_t error);
void ModeManager_GetStatus(ModeStatus_t *status);
uint32_t ModeManager_GetCapabilities(void);
void ModeManager_RegisterTasks(void);
void ModeManager_ScheduleReset(void);
void ModeManager_HealthPoll(void);

#endif /* MODE_MANAGER_H */
