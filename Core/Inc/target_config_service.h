#ifndef TARGET_CONFIG_SERVICE_H
#define TARGET_CONFIG_SERVICE_H

#include <stdint.h>

#define TARGET_CFG_PROTOCOL_VERSION 1U
#define TARGET_CFG_TRANSACTION_TIMEOUT_MS 30000U

typedef enum
{
  TARGET_CFG_SOURCE_GATT = 1,
  TARGET_CFG_SOURCE_MESH = 2
} TargetCfgSource;

typedef enum
{
  TARGET_CFG_OK = 0,
  TARGET_CFG_BAD_LENGTH = 1,
  TARGET_CFG_OUT_OF_RANGE = 2,
  TARGET_CFG_NOT_SUPPORTED = 3,
  TARGET_CFG_BAD_STATE = 4,
  TARGET_CFG_INCONSISTENT = 5,
  TARGET_CFG_EEPROM_ERROR = 6,
  TARGET_CFG_BUSY = 7,
  TARGET_CFG_BAD_TRANSACTION = 8
} TargetCfgStatus;

enum
{
  TARGET_CFG_BEGIN = 0x29,
  TARGET_CFG_FIELD_WRITE = 0x2A,
  TARGET_CFG_COMMIT = 0x2B,
  TARGET_CFG_ABORT = 0x2C,
  TARGET_CFG_FIELD_READ = 0x2D,
  TARGET_CFG_STATE_READ = 0x2E,
  TARGET_ACTIVE_SURFACE_SET = 0x2F,
  TARGET_ACTIVE_SURFACE_READ = 0x30,
  TARGET_RADIUS_SET = 0x31,
  TARGET_RADIUS_READ = 0x32,
  TARGET_ZEROING_HOLD_SET = 0x33,
  TARGET_ZEROING_HOLD_READ = 0x34
};

enum
{
  TARGET_FIELD_ASSET_ID = 1,
  TARGET_FIELD_OWNER_ID = 2,
  TARGET_FIELD_OWNER_LABEL = 3,
  TARGET_FIELD_SITE_ID = 4,
  TARGET_FIELD_SITE_LABEL = 5,
  TARGET_FIELD_DEST_MESH_NETWORK_ID = 6,
  TARGET_FIELD_NETWORK_LABEL = 7,
  TARGET_FIELD_SHOT_LINE_NBR = 8,
  TARGET_FIELD_MOUNT_POSITION = 9,
  TARGET_FIELD_SURFACE_KIND = 10,
  TARGET_FIELD_TARGET_TYPE = 11
};

enum { TARGET_ACTIVE_SURFACE_STANDING = 1, TARGET_ACTIVE_SURFACE_PRONE = 2 };
enum { TARGET_RADIUS_STANDING = 1, TARGET_RADIUS_PRONE = 2 };

TargetCfgStatus TargetConfigService_Handle(TargetCfgSource source,
                                           uint8_t subcommand,
                                           const uint8_t *payload,
                                           uint8_t payload_len,
                                           uint8_t *response,
                                           uint8_t *response_len);
void TargetConfigService_Abort(TargetCfgSource source);
uint8_t TargetRuntimeService_IsZeroingHoldEnabled(void);

#endif
