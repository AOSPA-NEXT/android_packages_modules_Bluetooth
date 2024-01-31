/******************************************************************************
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 ******************************************************************************/

/******************************************************************************
 *
 * This file contains functions that handle BTM Vendor interface functions for
 * the Bluetooth Add ON feature, etc.
 *
 ******************************************************************************/

#include <base/logging.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <include/hardware/bt_av.h>
#include "acl_api.h"
#include "btconfigstore/bt_configstore.h"
#include "btif/include/btif_config.h"
#include "btm_api.h"
#include "btm_int_types.h"
#include "osi/include/log.h"
#include "stack/acl/acl.h"
#include "stack/acl/peer_packet_types.h"
#include "stack/include/bt_types.h"
#include "stack/include/btm_iso_api.h"
#include "stack/include/btm_vendor_api.h"
#include "stack/include/btm_vendor_types.h"

#define QHS_TRANSPORT_BREDR 0
#define QHS_TRANSPORT_LE 1
#define QHS_TRANSPORT_LE_ISO 2

/* Disable QHS */
#define QHS_HOST_MODE_HOST_DISABLE 0
/* Enable QHS support */
#define QHS_HOST_MODE_HOST_AWARE 3
/* Disable QHS, QLL and QLMP modes */
#define QHS_HOST_DISABLE_ALL 4

#define QHS_BREDR_MASK 0x01
#define QHS_LE_MASK 0x02
#define QHS_LE_ISO_MASK 0x04

#define QBCE_QLL_MULTI_CONFIG_CIS_PARAMETER_UPDATE_HOST_BIT 58

typedef struct {
  const uint8_t as_array[8];
} bt_event_mask_t;

const bt_event_mask_t QBCE_QLM_AND_QLL_EVENT_MASK = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x4A}};

// BT features related defines
static bt_soc_type_t soc_type = BT_SOC_TYPE_DEFAULT;
static char a2dp_offload_Cap[PROPERTY_VALUE_MAX] = {'\0'};
static bool spilt_a2dp_supported = true;
static bool aac_frame_ctl_enabled = false;
static bool max_power_prop_enabled = false;
static uint8_t max_power_prop_value[3];
static uint8_t scrambling_supported_freqs[MAX_SUPPORTED_SCRAMBLING_FREQ_SIZE];
static uint8_t number_of_scrambling_supported_freqs = 0;
static bt_device_soc_add_on_features_t soc_add_on_features;
static uint8_t soc_add_on_features_length = 0;
static uint16_t product_id, response_version;
static bt_device_host_add_on_features_t host_add_on_features;
static uint8_t host_add_on_features_length = 0;
char qhs_value[PROPERTY_VALUE_MAX] = "0";
uint8_t qhs_support_mask = 0;
static bt_device_qll_local_supported_features_t qll_features;
static bt_configstore_interface_t* bt_configstore_intf = NULL;
tBTM_VS_EVT_CB* p_vnd_qle_cig_latency_changed_cb = nullptr;

extern tBTM_CB btm_cb;

void BTM_ConfigQHS();

static inline bool is_byte_valid(char ch) {
  return ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
          (ch >= 'A' && ch <= 'F'));
}

bool decode_max_power_values(char* power_val) {
  bool status = false;
  char *token, *reset = power_val;
  int i;

  if (!strcmp(power_val, "false")) {
    LOG_INFO(": MAX POW property is not set");
    return false;
  } else if (!strchr(power_val, '-') ||
             (!strchr(power_val, 'x') && !strchr(power_val, 'X')) ||
             strlen(power_val) != 14) {
    LOG_WARN(": MAX POW property is not in required order");
    return false;
  } else {
    status = true;
    for (i = 0; (i < 3 && status); i++) {
      token = strtok_r(reset, "-", &reset);
      if (token && strlen(token) == 4 &&
          (token[0] == '0' && (token[1] == 'x' || token[1] == 'X') &&
           (is_byte_valid(token[2]) && is_byte_valid(token[3])))) {
        max_power_prop_value[i] = (uint8_t)strtoul(token, NULL, 16);
      } else {
        status = false;
      }
    }
  }

  if (status) {
    LOG_DEBUG(
        ": MAX_POW_ID: BR MAX POW:%02x, EDR MAX POW:%02x, BLE MAX POW:%02x",
        max_power_prop_value[0], max_power_prop_value[1],
        max_power_prop_value[2]);
  } else {
    LOG_ERROR(": MAX POW property is not in required order");
  }

  return status;
}

char* BTM_GetA2dpOffloadCapablity() { return &a2dp_offload_Cap[0]; }

bool BTM_IsSpiltA2dpSupported() { return spilt_a2dp_supported; }

bool BTM_IsAACFrameCtrlEnabled() { return aac_frame_ctl_enabled; }

uint8_t* BTM_GetScramblingSupportedFreqs(uint8_t* number_of_freqs) {
  if (number_of_scrambling_supported_freqs) {
    *number_of_freqs = number_of_scrambling_supported_freqs;
    return scrambling_supported_freqs;
  }
  return NULL;
}

void BTM_RegisterForQleCigLatencyChangedEvt(
    tBTM_VS_EVT_CB* qle_cig_latency_changed_cb) {
  p_vnd_qle_cig_latency_changed_cb = qle_cig_latency_changed_cb;
}

/*******************************************************************************
 *
 * Function         BTM_GetHostAddOnFeatures
 *
 * Description      BTM_GetHostAddOnFeatures
 *
 *
 * Returns          host add on features array
 *
 ******************************************************************************/
bt_device_host_add_on_features_t* BTM_GetHostAddOnFeatures(
    uint8_t* host_add_on_features_len) {
  *host_add_on_features_len = host_add_on_features_length;
  return &host_add_on_features;
}

/*******************************************************************************
 *
 * Function         BTM_GetSocAddOnFeatures
 *
 * Description      BTM_GetSocAddOnFeatures
 *
 *
 * Returns          soc add on features array
 *
 ******************************************************************************/
bt_device_soc_add_on_features_t* BTM_GetSocAddOnFeatures(
    uint8_t* soc_add_on_features_len) {
  *soc_add_on_features_len = soc_add_on_features_length;
  return &soc_add_on_features;
}
/*******************************************************************************
 *
 * Function         BTM_BleIsCisParamUpdateLocalHostSupported
 *
 * Description      This function is called to determine if
 *CIS_Parameter_Update_Host feature is supported by local host.
 *
 * Returns          bool true if supported, false otherwise
 *
 ******************************************************************************/
bool BTM_BleIsCisParamUpdateLocalHostSupported() {
  bool supported = false;

  char value[PROPERTY_VALUE_MAX] = "true";
  property_get("persist.vendor.service.bt.cis_param_update_enabled", value,
               "true");
  if (!strncmp("true", value, 4)) supported = true;

  LOG_INFO(": supported=%d", supported);

  return supported;
}

/*******************************************************************************
 *
 * Function         BTM_GetRemoteQLLFeatures
 *
 * Description      This function is called to get remote QLL features
 *
 * Parameters       features - 8 bytes array for features value
 *
 * Returns          true if feature value is available
 *
 ******************************************************************************/
bool BTM_GetRemoteQLLFeatures(uint16_t handle, uint8_t* features) {
  int idx;
  bool res = false;

  tACL_CONN* p_acl;

  if (!BTM_QBCE_QLE_HCI_SUPPORTED(soc_add_on_features.as_array)) {
    LOG_INFO("QHS not support");
    return false;
  }

  const RawAddress remote_bd_addr = acl_address_from_handle(handle);
  if (remote_bd_addr == RawAddress::kEmpty) {
    LOG_ERROR("can't find acl for handle: 0x%04x", handle);
    return false;
  }

  p_acl = btm_acl_for_bda(remote_bd_addr, BT_TRANSPORT_LE);

  if (p_acl == nullptr) {
    LOG_ERROR("can't find acl for handle: 0x%04x", handle);
    return false;
  }

  LOG_INFO(" : qll_features_state = %x", p_acl->qll_features_state);

  if (p_acl->qll_features_state != BTM_QLL_FEATURES_STATE_FEATURE_COMPLETE) {
    BD_FEATURES value;
    size_t length = sizeof(value);

    if (btif_config_get_bin(p_acl->remote_addr.ToString().c_str(),
                            "QLL_FEATURES", value, &length)) {
      LOG_INFO("reading feature from config file");
      p_acl->qll_features_state = BTM_QLL_FEATURES_STATE_FEATURE_COMPLETE;
      memcpy(p_acl->remote_qll_features, value, BD_FEATURES_LEN);
      res = true;
    }
  } else {
    res = true;
  }

  if (res && features) {
    memcpy(features, p_acl->remote_qll_features, BD_FEATURES_LEN);
  }
  return res;
}

static void qbce_set_qhs_host_mode_hci_cmd_complete(tBTM_VSC_CMPL* p_data) {
  uint8_t *stream, status, subcmd;
  uint16_t opcode, length;

  if (p_data && (stream = (uint8_t*)p_data->p_param_buf)) {
    opcode = p_data->opcode;
    length = p_data->param_len;
    STREAM_TO_UINT8(status, stream);
    STREAM_TO_UINT8(subcmd, stream);
    LOG_INFO(": opcode = 0x%04X, length = %d, status = %d, subcmd = %d", opcode,
             length, status, subcmd);
    if (status == HCI_SUCCESS) {
      LOG_INFO(": status success");
    }
  }
}

static void qbce_set_qll_event_mask_hci_cmd_complete(tBTM_VSC_CMPL* p_data) {
  uint8_t *stream, status, subcmd;
  uint16_t opcode, length;

  if (p_data && (stream = (uint8_t*)p_data->p_param_buf)) {
    opcode = p_data->opcode;
    length = p_data->param_len;
    STREAM_TO_UINT8(status, stream);
    STREAM_TO_UINT8(subcmd, stream);
    LOG_INFO(": opcode = 0x%04X, length = %d, status = %d, subcmd = %d", opcode,
             length, status, subcmd);
    if (status == HCI_SUCCESS) {
      LOG_INFO(": status success");
    }
  }
}

static void qbce_set_qlm_event_mask_hci_cmd_complete(tBTM_VSC_CMPL* p_data) {
  uint8_t *stream, status, subcmd;
  uint16_t opcode, length;

  if (p_data && (stream = (uint8_t*)p_data->p_param_buf)) {
    opcode = p_data->opcode;
    length = p_data->param_len;
    STREAM_TO_UINT8(status, stream);
    STREAM_TO_UINT8(subcmd, stream);

    LOG_INFO(": opcode = 0x%04X, length = %d, status = %d, subcmd = %d", opcode,
             length, status, subcmd);
    if (status == HCI_SUCCESS) {
      LOG_INFO(": status success");
    }
  }
}

static void qbce_qle_set_host_feature_hci_cmd_complete(tBTM_VSC_CMPL* p_data) {
  uint8_t *stream, status, subcmd;
  uint16_t opcode, length;

  if (p_data && (stream = (uint8_t*)p_data->p_param_buf)) {
    opcode = p_data->opcode;
    length = p_data->param_len;
    STREAM_TO_UINT8(status, stream);
    STREAM_TO_UINT8(subcmd, stream);
    LOG_INFO(": opcode = 0x%04X, length = %d, status = %d, subcmd = %d", opcode,
             length, status, subcmd);
    if (status == HCI_SUCCESS) {
      LOG_INFO(": Status success");
    }
  }
}
static void parse_qll_read_local_supported_features_response(
    tBTM_VSC_CMPL* p_data) {
  uint8_t *stream, status, subcmd;
  uint16_t opcode, length;

  if (p_data && (stream = (uint8_t*)p_data->p_param_buf)) {
    opcode = p_data->opcode;
    length = p_data->param_len;
    STREAM_TO_UINT8(status, stream);
    STREAM_TO_UINT8(subcmd, stream);
    STREAM_TO_ARRAY(qll_features.as_array, stream,
                    (int)sizeof(bt_device_qll_local_supported_features_t));
    LOG_INFO(": opcode = 0x%04X, length = %d, status = %d, subcmd = %d", opcode,
             length, status, subcmd);
    if (status == HCI_SUCCESS) {
      LOG_INFO(": status success");
      if (BTM_QBCE_QLL_MULTI_CONFIG_CIS_PARAMETER_UPDATE_CONTROLLER(
              qll_features.as_array) &&
          BTM_BleIsCisParamUpdateLocalHostSupported()) {
        uint8_t cmd[3];
        cmd[0] = QBCE_QLE_SET_HOST_FEATURE;
        cmd[1] = QBCE_QLL_MULTI_CONFIG_CIS_PARAMETER_UPDATE_HOST_BIT;
        cmd[2] = 1;
        BTM_VendorSpecificCommand(HCI_VS_QBCE_OCF, sizeof(cmd), cmd,
                                  qbce_qle_set_host_feature_hci_cmd_complete);
      }
    }
  }
}

static void parse_controller_addon_features_response(tBTM_VSC_CMPL* p_data) {
  uint8_t *stream, status;
  uint16_t opcode, length;

  if (p_data && (stream = (uint8_t*)p_data->p_param_buf)) {
    opcode = p_data->opcode;
    length = p_data->param_len;
    STREAM_TO_UINT8(status, stream);

    if (stream && (length > 8)) {
      STREAM_TO_UINT16(product_id, stream);
      STREAM_TO_UINT16(response_version, stream);

      soc_add_on_features_length = length - 5;
      STREAM_TO_ARRAY(soc_add_on_features.as_array, stream,
                      soc_add_on_features_length);
      soc_add_on_features.as_array[soc_add_on_features_length] = '\0';
    }

    LOG_INFO(
        " ::opcode = 0x%04X, length = %d, soc_add_on_features_length=%d status "
        "= %d, product_id:%d, feature=%s",
        opcode, length, soc_add_on_features_length, status, product_id,
        soc_add_on_features.as_array);
    if (status == HCI_SUCCESS) {
      LOG_INFO(": status success");

      if (BTM_SPLIT_A2DP_SCRAMBLING_DATA_REQUIRED(
              soc_add_on_features.as_array)) {
        if (BTM_SPLIT_A2DP_44P1KHZ_SAMPLE_FREQ(soc_add_on_features.as_array)) {
          scrambling_supported_freqs[number_of_scrambling_supported_freqs++] =
              BTAV_A2DP_CODEC_SAMPLE_RATE_44100;
          scrambling_supported_freqs[number_of_scrambling_supported_freqs++] =
              BTAV_A2DP_CODEC_SAMPLE_RATE_88200;
        }
        if (BTM_SPLIT_A2DP_48KHZ_SAMPLE_FREQ(soc_add_on_features.as_array)) {
          scrambling_supported_freqs[number_of_scrambling_supported_freqs++] =
              BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
          scrambling_supported_freqs[number_of_scrambling_supported_freqs++] =
              BTAV_A2DP_CODEC_SAMPLE_RATE_96000;
        }
      }
      BTM_ConfigQHS();
    }
  }
}

void btm_ble_read_remote_supported_qll_features_status_cback(
    tBTM_VSC_CMPL* param) {
  uint8_t status;

  LOG_INFO(" :: op: %x, param_len: %d", param->opcode, param->param_len);
  if (param->param_len == 1) {
    status = *param->p_param_buf;
    LOG_INFO(" :: status = %d", status);
  }
}

/*******************************************************************************
 *
 * Function         btm_ble_qll_connection_complete
 *
 * Description      This function process the QLL connection complete event.
 *
 * Returns          void
 *
 ******************************************************************************/
void btm_ble_qll_connection_complete(uint8_t* p) {
  uint16_t handle;
  uint8_t status, param[3] = {0}, *p_param = param;
  int idx;
  tACL_CONN* p_acl;

  STREAM_TO_UINT8(status, p);
  STREAM_TO_UINT16(handle, p);
  handle = handle & 0x0FFF;

  const RawAddress remote_bd_addr = acl_address_from_handle(handle);
  if (remote_bd_addr == RawAddress::kEmpty) {
    LOG_ERROR(" :: can't find acl for handle: 0x%04x", handle);
    return;
  }

  p_acl = btm_acl_for_bda(remote_bd_addr, BT_TRANSPORT_LE);

  if (p_acl == nullptr) {
    LOG_ERROR(" :: can't find acl for handle: 0x%04x", handle);
    return;
  }

  if (status != HCI_SUCCESS) {
    LOG_ERROR(" :: failed for handle: 0x%04x, status 0x%02x", handle, status);
    p_acl->qll_features_state = BTM_QLL_FEATURES_STATE_ERROR;
    return;
  }

  p_acl->qll_features_state = BTM_QLL_FEATURES_STATE_CONN_COMPLETE;

  UINT8_TO_STREAM(p_param, QBCE_READ_REMOTE_QLL_SUPPORTED_FEATURE);
  UINT16_TO_STREAM(p_param, handle);
  BTM_VendorSpecificCommand(
      HCI_VS_QBCE_OCF, BTM_QBCE_READ_REMOTE_QLL_SUPPORTED_FEATURE_LEN, param,
      btm_ble_read_remote_supported_qll_features_status_cback);
}

/*******************************************************************************
 *
 * Function         btm_ble_read_remote_supported_qll_features_complete
 *
 * Description      This function process the read remote supported QLL features
 *                  complete event.
 *
 * Returns          void
 *
 ******************************************************************************/
void btm_ble_read_remote_supported_qll_features_complete(uint8_t* p) {
  uint16_t handle;
  uint8_t status;
  int idx;
  tACL_CONN* p_acl;

  STREAM_TO_UINT8(status, p);
  STREAM_TO_UINT16(handle, p);
  handle = handle & 0x0FFF;

  const RawAddress remote_bd_addr = acl_address_from_handle(handle);
  if (remote_bd_addr == RawAddress::kEmpty) {
    LOG_ERROR(" :: can't find acl for handle: 0x%04x", handle);
    return;
  }

  p_acl = btm_acl_for_bda(remote_bd_addr, BT_TRANSPORT_LE);

  if (p_acl == nullptr) {
    LOG_ERROR(":: can't find acl for handle: 0x%04x", handle);
    return;
  }

  if (status != HCI_SUCCESS) {
    LOG_ERROR(":: failed for handle: 0x%04x, status 0x%02x", handle, status);
    p_acl->qll_features_state = BTM_QLL_FEATURES_STATE_ERROR;
    return;
  }

  p_acl->qll_features_state = BTM_QLL_FEATURES_STATE_FEATURE_COMPLETE;
  STREAM_TO_ARRAY(p_acl->remote_qll_features, p, BD_FEATURES_LEN);
  btif_config_set_bin(p_acl->remote_addr.ToString(), "QLL_FEATURES",
                      p_acl->remote_qll_features, BD_FEATURES_LEN);
}

/*******************************************************************************
 *
 * Function        BTM_GetQcmPhyState
 *
 * Description     This function returns the phy state of ACL connection.
 *
 *
 * Parameters      bda : BD address of the remote device
 *
 * Returns         Returns qcm phy state of ACL connection.
 *                 Returns default value as BR/EDR if it fails.
 *
 *
 ******************************************************************************/
uint8_t BTM_GetQcmPhyState(const RawAddress& bda) {
  bool ret;
  // Default value for QCM PHY state
  int qcm_phy_state = QCM_PHY_STATE_BR_EDR;

  ret = btif_config_get_int(bda.ToString(), "QCM_PHY_STATE", &qcm_phy_state);
  if (ret == 0) {
    LOG_ERROR(" :: can't find phy state for BdAddr %s in btconfig file",
              bda.ToString().c_str());
  }
  return (uint8_t)qcm_phy_state;
}

/*******************************************************************************
 *
 * Function        btm_acl_update_qcm_phy_state
 *
 * Description     This function updates the qcm phy state of ACL connection.
 *
 * Returns         void
 *
 ******************************************************************************/
void btm_acl_update_qcm_phy_state(uint8_t* p) {
  uint16_t handle;
  uint8_t status, qcm_phy_state;
  int idx;
  tACL_CONN* p_acl;

  STREAM_TO_UINT8(status, p);
  STREAM_TO_UINT16(handle, p);

  handle = handle & 0x0FFF;

  const RawAddress remote_bd_addr = acl_address_from_handle(handle);
  if (remote_bd_addr == RawAddress::kEmpty) {
    LOG_ERROR(" :: can't find acl for handle: 0x%04x", handle);
    return;
  }

  p_acl = btm_acl_for_bda(remote_bd_addr, BT_TRANSPORT_LE);

  if (p_acl == nullptr) {
    LOG_ERROR(" :: can't find acl for handle: 0x%04x", handle);
    return;
  }

  if (status != HCI_SUCCESS) {
    LOG_ERROR(" :: failed for handle: 0x%04x, status 0x%02x", handle, status);
    // Setting qcm phy state to default value: 0x00 BR/EDR
    btif_config_set_int(p_acl->remote_addr.ToString(), "QCM_PHY_STATE",
                        QCM_PHY_STATE_BR_EDR);
    return;
  }

  STREAM_TO_UINT8(qcm_phy_state, p);
  // Setting qcm phy state as 0x00 BR/EDR, 0x01 QHS
  btif_config_set_int(p_acl->remote_addr.ToString(), "QCM_PHY_STATE",
                      qcm_phy_state);
}

/*******************************************************************************
 *
 * Function        BTM_IsQHSPhySupported
 *
 * Description     This function is called to determine if QHS is supported or
 *not.
 *
 * Parameters      bda : BD address of the remote device
 *                 transport : Physical transport used for ACL connection
 *                 (BR/EDR or LE)
 *
 * Returns         True if qhs phy can be used, false otherwise.
 *
 ******************************************************************************/
bool BTM_IsQHSPhySupported(const RawAddress& bda, tBT_TRANSPORT transport) {
  bool qhs_phy = false;
  if (transport == BT_TRANSPORT_LE) {
    tACL_CONN* p_acl = btm_acl_for_bda(bda, BT_TRANSPORT_LE);
    if (p_acl == NULL) {
      LOG_ERROR("invalid bda %s", bda.ToString().c_str());
      qhs_phy = false;
    } else {
      bool ret;
      BD_FEATURES features;

      ret = BTM_GetRemoteQLLFeatures(p_acl->hci_handle, (uint8_t*)&features);
      if (ret && (features[2] & 0x40)) qhs_phy = true;
    }
  }

  if (transport == BT_TRANSPORT_BR_EDR) {
    uint8_t qcm_phy_state = BTM_GetQcmPhyState(bda);
    if (qcm_phy_state == QCM_PHY_STATE_QHS) {
      qhs_phy = true;
    }
  }
  if (qhs_phy == false) {
    LOG_DEBUG(": QHS not supported for transport = %d and BdAddr = %s",
              transport, bda.ToString().c_str());
  }
  return qhs_phy;
}

/*******************************************************************************
 *
 * Function         btm_vendor_vse_cback
 *
 * Description      Process event VENDOR_SPECIFIC_EVT
 *
 * Returns          void
 *
 ******************************************************************************/
void btm_vendor_vse_cback(uint8_t* p, uint8_t evt_len) {
  uint8_t i;
  uint8_t* pp = p;
  uint8_t vse_subcode;

  if (evt_len >= 2) {
    STREAM_TO_UINT8(vse_subcode, pp);

    if (HCI_VSE_SUBCODE_QBCE == vse_subcode) {
      uint8_t vse_msg_type;

      STREAM_TO_UINT8(vse_msg_type, pp);
      LOG_INFO(" :: QBCE VSE event received, msg = %x", vse_msg_type);
      switch (vse_msg_type) {
        case MSG_QBCE_QLL_CONNECTION_COMPLETE:
          btm_ble_qll_connection_complete(pp);
          break;
        case MSG_QBCE_REMOTE_SUPPORTED_QLL_FEATURES_COMPLETE:
          btm_ble_read_remote_supported_qll_features_complete(pp);
          break;
        case MSG_QBCE_QCM_PHY_CHANGE:
          btm_acl_update_qcm_phy_state(pp);
          break;
        case MSG_QBCE_QLE_CIG_LATENCY_CHANGED:
          if (p_vnd_qle_cig_latency_changed_cb != nullptr) {
            LOG_INFO("Calling qle_cig_latency_changed_cb");
            (*p_vnd_qle_cig_latency_changed_cb)((evt_len - 2), pp);
            return;
          }
          break;
        case MSG_QBCE_VS_PARAM_REPORT_EVENT:
          bluetooth::hci::IsoManager::GetInstance()->HandleVscHciEvent(
              vse_msg_type, p, evt_len - 1);
        default:
          LOG_INFO(" :: unknown msg type: %d", vse_msg_type);
          break;
      }
      return;
    }
  }
  LOG_DEBUG("BTM Event: Vendor Specific event from controller");
}

void BTM_ConfigQHS() {
  if (BTM_QBCE_QLE_HCI_SUPPORTED(soc_add_on_features.as_array)) {
    BT_HDR* response;
    char qhs_iso[PROPERTY_VALUE_MAX] = "false";
    property_get("persist.vendor.btstack.qhs_enable", qhs_iso, "true");
    uint8_t cmd[3];
    uint8_t sub_cmd = QBCE_SET_QHS_HOST_MODE;

    memset(cmd, 0, 3);

    cmd[0] = sub_cmd;
    cmd[1] = QHS_TRANSPORT_LE_ISO;

    if (!strncmp("true", qhs_iso, 4)) {
      cmd[2] = QHS_HOST_MODE_HOST_AWARE;
    } else {
      cmd[2] = QHS_HOST_DISABLE_ALL;
    }
    BTM_VendorSpecificCommand(HCI_VS_QBCE_OCF, sizeof(cmd), cmd,
                              qbce_set_qhs_host_mode_hci_cmd_complete);
    /* This property is for test/debug purpose only */
    property_get("persist.vendor.btstack.qhs_support", qhs_value, "255");
    LOG_INFO(": qhs property value= %s", qhs_value);
    qhs_support_mask = (uint8_t)atoi(qhs_value);
    LOG_INFO(": qhs support mask=%d", qhs_support_mask);
    if (qhs_support_mask != 0xFF) {
      if (qhs_support_mask & QHS_BREDR_MASK) {
        cmd[1] = QHS_TRANSPORT_BREDR;
        cmd[2] = QHS_HOST_MODE_HOST_AWARE;
      } else {
        cmd[1] = QHS_TRANSPORT_BREDR;
        cmd[2] = QHS_HOST_DISABLE_ALL;
      }
      BTM_VendorSpecificCommand(HCI_VS_QBCE_OCF, sizeof(cmd), cmd,
                                qbce_set_qhs_host_mode_hci_cmd_complete);
      if (qhs_support_mask & QHS_LE_MASK) {
        cmd[1] = QHS_TRANSPORT_LE;
        cmd[2] = QHS_HOST_MODE_HOST_AWARE;
      } else {
        cmd[1] = QHS_TRANSPORT_LE;
        cmd[2] = QHS_HOST_DISABLE_ALL;
      }
      BTM_VendorSpecificCommand(HCI_VS_QBCE_OCF, sizeof(cmd), cmd,
                                qbce_set_qhs_host_mode_hci_cmd_complete);
      if (qhs_support_mask & QHS_LE_MASK) {
        cmd[1] = QHS_TRANSPORT_LE_ISO;
        cmd[2] = QHS_HOST_MODE_HOST_AWARE;
      } else {
        cmd[1] = QHS_TRANSPORT_LE_ISO;
        cmd[2] = QHS_HOST_DISABLE_ALL;
      }
      BTM_VendorSpecificCommand(HCI_VS_QBCE_OCF, sizeof(cmd), cmd,
                                qbce_set_qhs_host_mode_hci_cmd_complete);
    }
    uint8_t cmd_qll[9];
    uint8_t* stream = &cmd_qll[0];
    UINT8_TO_STREAM(stream, QBCE_SET_QLL_EVENT_MASK);
    ARRAY8_TO_STREAM(stream, (&QBCE_QLM_AND_QLL_EVENT_MASK)->as_array);

    BTM_VendorSpecificCommand(HCI_VS_QBCE_OCF, sizeof(cmd_qll), cmd_qll,
                              qbce_set_qll_event_mask_hci_cmd_complete);
  }

  if (BTM_QBCE_QCM_HCI_SUPPORTED(soc_add_on_features.as_array)) {
    uint8_t cmd_qlm[9];
    uint8_t* stream = &cmd_qlm[0];
    ;
    UINT8_TO_STREAM(stream, QBCE_SET_QLM_EVENT_MASK);
    ARRAY8_TO_STREAM(stream, (&QBCE_QLM_AND_QLL_EVENT_MASK)->as_array);

    BTM_VendorSpecificCommand(HCI_VS_QBCE_OCF, sizeof(cmd_qlm), cmd_qlm,
                              qbce_set_qlm_event_mask_hci_cmd_complete);
  }

  if (BTM_QBCE_QLE_HCI_SUPPORTED(soc_add_on_features.as_array)) {
    uint8_t cmd[1];
    cmd[0] = QBCE_READ_LOCAL_QLL_SUPPORTED_FEATURES;

    BTM_VendorSpecificCommand(HCI_VS_QBCE_OCF, sizeof(cmd), cmd,
                              parse_qll_read_local_supported_features_response);
  }
}

void BTM_ReadVendorAddOnFeaturesInternal() {
  bt_configstore_intf = get_btConfigStore_interface();
  BTM_RegisterForVSEvents((tBTM_VS_EVT_CB*)btm_vendor_vse_cback, true);
  if (bt_configstore_intf != NULL) {
    std::vector<vendor_property_t> vPropList;
    bt_configstore_intf->get_vendor_properties(BT_PROP_ALL, vPropList);

    for (auto&& vendorProp : vPropList) {
      switch (vendorProp.type) {
        case BT_PROP_SOC_TYPE:
          char soc_name[32];

          strlcpy(soc_name, vendorProp.value, sizeof(soc_name));
          soc_type =
              bt_configstore_intf->convert_bt_soc_name_to_soc_type(soc_name);
          LOG_INFO(": soc_name:%s, soc_type = %d", soc_name, soc_type);
          break;

        case BT_PROP_A2DP_OFFLOAD_CAP:
          strlcpy(a2dp_offload_Cap, vendorProp.value, sizeof(a2dp_offload_Cap));
          LOG_INFO(": a2dp_offload_Cap = %s", a2dp_offload_Cap);
          break;

        case BT_PROP_SPILT_A2DP:
          if (!strncasecmp(vendorProp.value, "true", sizeof("true"))) {
            spilt_a2dp_supported = true;
          } else {
            spilt_a2dp_supported = false;
          }

          LOG_INFO(":: spilt_a2dp_supported = %d", spilt_a2dp_supported);
          break;

        case BT_PROP_AAC_FRAME_CTL:
          if (!strncasecmp(vendorProp.value, "true", sizeof("true"))) {
            aac_frame_ctl_enabled = true;
          } else {
            aac_frame_ctl_enabled = false;
          }

          LOG_INFO(": aac_frame_ctl_enabled = %d", aac_frame_ctl_enabled);
          break;

        case BT_PROP_MAX_POWER:
          max_power_prop_enabled =
              decode_max_power_values((char*)vendorProp.value);
          LOG_INFO(": max_power_prop_enabled = %d", max_power_prop_enabled);
          break;
        default:
          break;
      }
    }
    host_add_on_features_list_t features_list;

    if (bt_configstore_intf->get_host_add_on_features(&features_list)) {
      host_add_on_features_length = features_list.feat_mask_len;
      if (host_add_on_features_length != 0 &&
          host_add_on_features_length <= HOST_ADD_ON_FEATURES_MAX_SIZE)
        memcpy(host_add_on_features.as_array, features_list.features,
               host_add_on_features_length);
    }

    // Read HCI_VS_GET_ADDON_FEATURES_SUPPORT
    if (soc_type >= BT_SOC_TYPE_CHEROKEE) {
      controller_add_on_features_list_t features_list;

      if (bt_configstore_intf->get_controller_add_on_features(&features_list)) {
        product_id = features_list.product_id;
        response_version = features_list.rsp_version;
        soc_add_on_features_length = features_list.feat_mask_len;
        if (soc_add_on_features_length != 0) {
          if (soc_add_on_features_length <= SOC_ADD_ON_FEATURES_MAX_SIZE) {
            memcpy(soc_add_on_features.as_array, features_list.features,
                   soc_add_on_features_length);
            if (BTM_SPLIT_A2DP_SCRAMBLING_DATA_REQUIRED(
                    soc_add_on_features.as_array)) {
              if (BTM_SPLIT_A2DP_44P1KHZ_SAMPLE_FREQ(
                      soc_add_on_features.as_array)) {
                scrambling_supported_freqs
                    [number_of_scrambling_supported_freqs++] =
                        BTAV_A2DP_CODEC_SAMPLE_RATE_44100;
                scrambling_supported_freqs
                    [number_of_scrambling_supported_freqs++] =
                        BTAV_A2DP_CODEC_SAMPLE_RATE_88200;
              }
              if (BTM_SPLIT_A2DP_48KHZ_SAMPLE_FREQ(
                      soc_add_on_features.as_array)) {
                scrambling_supported_freqs
                    [number_of_scrambling_supported_freqs++] =
                        BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
                scrambling_supported_freqs
                    [number_of_scrambling_supported_freqs++] =
                        BTAV_A2DP_CODEC_SAMPLE_RATE_96000;
              }
            }
            BTM_ConfigQHS();
          } else {
            LOG(FATAL) << __func__ << "invalid soc add on features length: "
                       << +soc_add_on_features_length;
          }
        }
      }
    }
  }
}

/*******************************************************************************
 *
 * Function         BTM_ReadVendorAddOnFeatures
 *
 * Description      BTM_ReadVendorAddOnFeatures
 *
 * Parameters:      None
 *
 ******************************************************************************/
void BTM_ReadVendorAddOnFeatures() {
  bool btConfigStore = true;
  char bt_config_store_prop[PROPERTY_VALUE_MAX] = {'\0'};
  int ret = 0;

  ret = property_get("ro.vendor.bluetooth.btconfigstore", bt_config_store_prop,
                     "true");

  if (ret != 0) {
    if (!strncasecmp(bt_config_store_prop, "true", sizeof("true"))) {
      btConfigStore = true;
    } else {
      btConfigStore = false;
    }
    LOG_INFO(":: btConfigStore = %d", btConfigStore);
  }

  if (btConfigStore) {
    BTM_ReadVendorAddOnFeaturesInternal();
  } else {
    LOG_INFO(": Soc Add On");

    char soc_name[PROPERTY_VALUE_MAX] = {'\0'};
    char splita2dp[PROPERTY_VALUE_MAX];
    char aac_frame_ctl[PROPERTY_VALUE_MAX];
    char max_pow_support[PROPERTY_VALUE_MAX];

    ret = property_get("persist.vendor.qcom.bluetooth.soc", soc_name, "");
    LOG_INFO(" :: Bluetooth soc type set to: %s, ret: %d", soc_name, ret);

    if (ret != 0) {
      bt_configstore_intf = get_btConfigStore_interface();
      soc_type = bt_configstore_intf->convert_bt_soc_name_to_soc_type(soc_name);
      LOG_INFO(": soc_name:%s, soc_type = %d", soc_name, soc_type);
    }

    ret = property_get("persist.vendor.qcom.bluetooth.enable.splita2dp",
                       splita2dp, "true");
    LOG_INFO(":: persist.vendor.qcom.bluetooth.enable.splita2dp: %s, ret: %d",
             splita2dp, ret);

    if (ret != 0) {
      if (!strncasecmp(splita2dp, "true", sizeof("true"))) {
        spilt_a2dp_supported = true;
      } else {
        spilt_a2dp_supported = false;
      }
      LOG_INFO(":: spilt_a2dp_supported = %d", spilt_a2dp_supported);
    }

    ret = property_get("persist.vendor.qcom.bluetooth.a2dp_offload_cap",
                       a2dp_offload_Cap, "");
    LOG_INFO(" :: a2dp_offload_Cap = %s", a2dp_offload_Cap);

    ret = property_get("persist.vendor.qcom.bluetooth.aac_frm_ctl.enabled",
                       aac_frame_ctl, "false");
    LOG_INFO(
        " :: persist.vendor.qcom.bluetooth.aac_frm_ctl.enabled: %s, ret: %d",
        aac_frame_ctl, ret);

    if (ret != 0) {
      if (!strncasecmp(aac_frame_ctl, "true", sizeof("true"))) {
        aac_frame_ctl_enabled = true;
      } else {
        aac_frame_ctl_enabled = false;
      }
    }

    ret = property_get("persist.vendor.qcom.bluetooth.max_power_support",
                       max_pow_support, "false");
    LOG_INFO(":: persist.vendor.qcom.bluetooth.max_power_support: %s, ret: %d",
             max_pow_support, ret);

    if (ret != 0) {
      max_power_prop_enabled = decode_max_power_values((char*)max_pow_support);
      LOG_INFO(": max_power_prop_enabled = %d", max_power_prop_enabled);
    }

    if (soc_type >= BT_SOC_TYPE_CHEROKEE) {
      BTM_VendorSpecificCommand(HCI_VS_GET_ADDON_FEATURES_SUPPORT, 0, NULL,
                                parse_controller_addon_features_response);
    }

    /*if (!HCI_LE_CIS_MASTER_SUPPORT(features_ble.as_array)) {
      adv_audio_support_mask &= ~ADV_AUDIO_UNICAST_FEAT_MASK;
    }
    if (!HCI_LE_PERIODIC_SYNC_TRANSFER_SEND_SUPPORTED(features_ble.as_array)) {
      adv_audio_support_mask &= ~ADV_AUDIO_BCA_FEAT_MASK;
    }
    if (!HCI_LE_ISO_BROADCASTER_SUPPORTED(features_ble.as_array)) {
      adv_audio_support_mask &= ~ADV_AUDIO_BCS_FEAT_MASK;
    }
    snprintf(adv_audio_property, 2, "%d", adv_audio_support_mask);
    osi_property_set("persist.vendor.service.bt.adv_audio_mask",
    adv_audio_property); */
  }
}
