#include "target_config_service.h"

#include <stdbool.h>
#include <string.h>

#include "main.h"
#include "nfc_eeprom_mngt.h"

typedef struct
{
  bool active;
  TargetCfgSource source;
  uint16_t id;
  uint32_t deadline;
  Eeprom_mngt_Config_t staged;
} TargetConfigTransaction;

typedef struct
{
  uint8_t active_surface;
  uint16_t standing_radius;
  uint16_t prone_radius;
  uint8_t zeroing_hold;
} TargetRuntimeState;

static TargetConfigTransaction Transaction;
static uint16_t NextTransactionId;
static TargetRuntimeState Runtime = {
  TARGET_ACTIVE_SURFACE_STANDING, 575U, 225U, 0U
};

static uint16_t ReadLe16(const uint8_t *value)
{
  return (uint16_t)value[0] | ((uint16_t)value[1] << 8);
}

static void WriteLe16(uint8_t *value, uint16_t number)
{
  value[0] = (uint8_t)number;
  value[1] = (uint8_t)(number >> 8);
}

static bool TransactionExpired(void)
{
  return Transaction.active && ((int32_t)(HAL_GetTick() - Transaction.deadline) >= 0);
}

static TargetCfgStatus CheckTransaction(TargetCfgSource source, const uint8_t *payload,
                                        uint8_t payload_len)
{
  if (payload_len < 2U) return TARGET_CFG_BAD_LENGTH;
  if (TransactionExpired()) Transaction.active = false;
  if (!Transaction.active || (Transaction.source != source) ||
      (Transaction.id != ReadLe16(payload))) return TARGET_CFG_BAD_TRANSACTION;
  Transaction.deadline = HAL_GetTick() + TARGET_CFG_TRANSACTION_TIMEOUT_MS;
  return TARGET_CFG_OK;
}

static TargetCfgStatus WriteField(uint8_t field, const uint8_t *value, uint8_t length)
{
  Eeprom_mngt_Config_t *cfg = &Transaction.staged;
  char *label = NULL;
  uint8_t capacity = 0U;

  switch (field)
  {
    case TARGET_FIELD_ASSET_ID:
      if (length != 4U) return TARGET_CFG_BAD_LENGTH;
      memcpy(cfg->AssetIdLe, value, length); return TARGET_CFG_OK;
    case TARGET_FIELD_OWNER_ID:
      if (length != 2U) return TARGET_CFG_BAD_LENGTH;
      memcpy(cfg->OwnerIdLe, value, length); return TARGET_CFG_OK;
    case TARGET_FIELD_SITE_ID:
      if (length != 2U) return TARGET_CFG_BAD_LENGTH;
      memcpy(cfg->SiteIdLe, value, length); return TARGET_CFG_OK;
    case TARGET_FIELD_DEST_MESH_NETWORK_ID:
      if (length != 8U) return TARGET_CFG_BAD_LENGTH;
      memcpy(cfg->DestMeshNetworkIdLe, value, length); return TARGET_CFG_OK;
    case TARGET_FIELD_SHOT_LINE_NBR:
      if (length != 1U) return TARGET_CFG_BAD_LENGTH;
      if (value[0] == 0xFFU) return TARGET_CFG_OUT_OF_RANGE;
      cfg->ShotLineNbr = value[0]; return TARGET_CFG_OK;
    case TARGET_FIELD_MOUNT_POSITION:
      if (length != 1U) return TARGET_CFG_BAD_LENGTH;
      if (value[0] > MOUNT_POSITION_LOWER) return TARGET_CFG_OUT_OF_RANGE;
      cfg->MountPosition = value[0]; return TARGET_CFG_OK;
    case TARGET_FIELD_SURFACE_KIND:
      if (length != 1U) return TARGET_CFG_BAD_LENGTH;
      if (value[0] > SURFACE_KIND_SHUTTERED) return TARGET_CFG_OUT_OF_RANGE;
      cfg->SurfaceKind = value[0]; return TARGET_CFG_OK;
    case TARGET_FIELD_TARGET_TYPE:
      if (length != 1U) return TARGET_CFG_BAD_LENGTH;
      if ((value[0] != MODE_SOLEMS) && (value[0] != MODE_PSD)) return TARGET_CFG_OUT_OF_RANGE;
      cfg->Mode = value[0]; return TARGET_CFG_OK;
    case TARGET_FIELD_OWNER_LABEL:
      label = cfg->OwnerLabel; capacity = sizeof(cfg->OwnerLabel); break;
    case TARGET_FIELD_SITE_LABEL:
      label = cfg->SiteLabel; capacity = sizeof(cfg->SiteLabel); break;
    case TARGET_FIELD_NETWORK_LABEL:
      label = cfg->NetworkLabel; capacity = sizeof(cfg->NetworkLabel); break;
    default:
      return TARGET_CFG_NOT_SUPPORTED;
  }
  if (length >= capacity) return TARGET_CFG_OUT_OF_RANGE;
  memset(label, 0, capacity);
  if (length > 0U) memcpy(label, value, length);
  return TARGET_CFG_OK;
}

static TargetCfgStatus ReadField(uint8_t field, uint8_t *response, uint8_t *response_len)
{
  Eeprom_mngt_Config_t cfg;
  const uint8_t *value = NULL;
  uint8_t length = 0U;
  EepromConfigGetSnapshot(&cfg);

  switch (field)
  {
    case TARGET_FIELD_ASSET_ID: value = cfg.AssetIdLe; length = 4U; break;
    case TARGET_FIELD_OWNER_ID: value = cfg.OwnerIdLe; length = 2U; break;
    case TARGET_FIELD_OWNER_LABEL: value = (uint8_t *)cfg.OwnerLabel; length = (uint8_t)strlen(cfg.OwnerLabel); break;
    case TARGET_FIELD_SITE_ID: value = cfg.SiteIdLe; length = 2U; break;
    case TARGET_FIELD_SITE_LABEL: value = (uint8_t *)cfg.SiteLabel; length = (uint8_t)strlen(cfg.SiteLabel); break;
    case TARGET_FIELD_DEST_MESH_NETWORK_ID: value = cfg.DestMeshNetworkIdLe; length = 8U; break;
    case TARGET_FIELD_NETWORK_LABEL: value = (uint8_t *)cfg.NetworkLabel; length = (uint8_t)strlen(cfg.NetworkLabel); break;
    case TARGET_FIELD_SHOT_LINE_NBR: value = &cfg.ShotLineNbr; length = 1U; break;
    case TARGET_FIELD_MOUNT_POSITION: value = &cfg.MountPosition; length = 1U; break;
    case TARGET_FIELD_SURFACE_KIND: value = &cfg.SurfaceKind; length = 1U; break;
    case TARGET_FIELD_TARGET_TYPE: value = &cfg.Mode; length = 1U; break;
    default: return TARGET_CFG_NOT_SUPPORTED;
  }
  response[0] = field;
  response[1] = length;
  if (length > 0U) memcpy(&response[2], value, length);
  *response_len = (uint8_t)(length + 2U);
  return TARGET_CFG_OK;
}

TargetCfgStatus TargetConfigService_Handle(TargetCfgSource source, uint8_t subcommand,
                                           const uint8_t *payload, uint8_t payload_len,
                                           uint8_t *response, uint8_t *response_len)
{
  TargetCfgStatus status;
  uint16_t generation;
  Eeprom_mngt_Config_t current;
  if ((response == NULL) || (response_len == NULL)) return TARGET_CFG_BAD_STATE;
  *response_len = 0U;

  switch (subcommand)
  {
    case TARGET_CFG_BEGIN:
      if (payload_len != 0U) return TARGET_CFG_BAD_LENGTH;
      if (TransactionExpired()) Transaction.active = false;
      if (Transaction.active) return TARGET_CFG_BUSY;
      EepromConfigGetSnapshot(&Transaction.staged);
      Transaction.active = true;
      Transaction.source = source;
      Transaction.id = ++NextTransactionId;
      if (Transaction.id == 0U) Transaction.id = ++NextTransactionId;
      Transaction.deadline = HAL_GetTick() + TARGET_CFG_TRANSACTION_TIMEOUT_MS;
      WriteLe16(response, Transaction.id); *response_len = 2U; return TARGET_CFG_OK;

    case TARGET_CFG_FIELD_WRITE:
      status = CheckTransaction(source, payload, payload_len);
      if (status != TARGET_CFG_OK) return status;
      if ((payload_len < 4U) || (payload_len != (uint8_t)(4U + payload[3]))) return TARGET_CFG_BAD_LENGTH;
      status = WriteField(payload[2], &payload[4], payload[3]);
      if (status == TARGET_CFG_OK) { response[0] = payload[2]; *response_len = 1U; }
      return status;

    case TARGET_CFG_COMMIT:
      status = CheckTransaction(source, payload, payload_len);
      if (status != TARGET_CFG_OK) return status;
      if (payload_len != 2U) return TARGET_CFG_BAD_LENGTH;
      if (!EepromConfigValidate(&Transaction.staged)) return TARGET_CFG_INCONSISTENT;
      generation = (uint16_t)(ReadLe16(Transaction.staged.AssignmentGenerationLe) + 1U);
      WriteLe16(Transaction.staged.AssignmentGenerationLe, generation);
      if (EepromConfigCommit(&Transaction.staged) != EEPROM_STORE_OK) return TARGET_CFG_EEPROM_ERROR;
      Transaction.active = false;
      WriteLe16(response, generation); *response_len = 2U;
      EepromNdefRefresh();
      return TARGET_CFG_OK;

    case TARGET_CFG_ABORT:
      status = CheckTransaction(source, payload, payload_len);
      if (status != TARGET_CFG_OK) return status;
      if (payload_len != 2U) return TARGET_CFG_BAD_LENGTH;
      Transaction.active = false; return TARGET_CFG_OK;

    case TARGET_CFG_FIELD_READ:
      if (payload_len != 1U) return TARGET_CFG_BAD_LENGTH;
      return ReadField(payload[0], response, response_len);

    case TARGET_CFG_STATE_READ:
      if (payload_len != 0U) return TARGET_CFG_BAD_LENGTH;
      EepromConfigGetSnapshot(&current);
      response[0] = TARGET_CFG_PROTOCOL_VERSION;
      response[1] = EepromConfigValidate(&current) ? 1U : 0U;
      response[2] = current.AssignmentGenerationLe[0];
      response[3] = current.AssignmentGenerationLe[1];
      *response_len = 4U; return TARGET_CFG_OK;

    case TARGET_ACTIVE_SURFACE_SET:
      if (payload_len != 1U) return TARGET_CFG_BAD_LENGTH;
      if (GetMode() != MODE_PSD) return TARGET_CFG_NOT_SUPPORTED;
      if ((payload[0] != TARGET_ACTIVE_SURFACE_STANDING) && (payload[0] != TARGET_ACTIVE_SURFACE_PRONE)) return TARGET_CFG_OUT_OF_RANGE;
      Runtime.active_surface = payload[0]; return TARGET_CFG_OK;
    case TARGET_ACTIVE_SURFACE_READ:
      if (payload_len != 0U) return TARGET_CFG_BAD_LENGTH;
      if (GetMode() != MODE_PSD) return TARGET_CFG_NOT_SUPPORTED;
      response[0] = Runtime.active_surface; *response_len = 1U; return TARGET_CFG_OK;
    case TARGET_RADIUS_SET:
      if (payload_len != 3U) return TARGET_CFG_BAD_LENGTH;
      if (GetMode() != MODE_PSD) return TARGET_CFG_NOT_SUPPORTED;
      if ((payload[0] != TARGET_RADIUS_STANDING) && (payload[0] != TARGET_RADIUS_PRONE)) return TARGET_CFG_OUT_OF_RANGE;
      if (ReadLe16(&payload[1]) == 0U) return TARGET_CFG_OUT_OF_RANGE;
      if (payload[0] == TARGET_RADIUS_STANDING) Runtime.standing_radius = ReadLe16(&payload[1]);
      else Runtime.prone_radius = ReadLe16(&payload[1]);
      return TARGET_CFG_OK;
    case TARGET_RADIUS_READ:
      if (payload_len != 1U) return TARGET_CFG_BAD_LENGTH;
      if (GetMode() != MODE_PSD) return TARGET_CFG_NOT_SUPPORTED;
      if ((payload[0] != TARGET_RADIUS_STANDING) && (payload[0] != TARGET_RADIUS_PRONE)) return TARGET_CFG_OUT_OF_RANGE;
      response[0] = payload[0];
      WriteLe16(&response[1], (payload[0] == TARGET_RADIUS_STANDING) ? Runtime.standing_radius : Runtime.prone_radius);
      *response_len = 3U; return TARGET_CFG_OK;
    case TARGET_ZEROING_HOLD_SET:
      if ((payload_len != 1U) || (payload[0] > 1U)) return (payload_len != 1U) ? TARGET_CFG_BAD_LENGTH : TARGET_CFG_OUT_OF_RANGE;
      Runtime.zeroing_hold = payload[0]; return TARGET_CFG_OK;
    case TARGET_ZEROING_HOLD_READ:
      if (payload_len != 0U) return TARGET_CFG_BAD_LENGTH;
      response[0] = Runtime.zeroing_hold; *response_len = 1U; return TARGET_CFG_OK;
    default:
      return TARGET_CFG_NOT_SUPPORTED;
  }
}

void TargetConfigService_Abort(TargetCfgSource source)
{
  if (Transaction.active && (Transaction.source == source)) Transaction.active = false;
}

uint8_t TargetRuntimeService_IsZeroingHoldEnabled(void)
{
  return Runtime.zeroing_hold;
}
