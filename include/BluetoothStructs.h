#ifndef BLUETOOTH_STRUCTS_H
#define BLUETOOTH_STRUCTS_H

// Include necessary headers
#include <cstdint>       // For fixed-width integer types
#include <cstring>
#include <map>
#include <memory>
#include <sys/socket.h>  // For sa_family_t

// Bluetooth Protocols
#define BTPROTO_L2CAP 0   ///< L2CAP protocol number
#define BTPROTO_HCI   1   ///< HCI protocol number

// Socket options and levels
#define SOL_HCI       0   ///< Socket level for HCI
#define HCI_FILTER    2   ///< Option name for HCI filter

// HCI ioctl commands
#define HCIGETDEVLIST _IOR('H', 210, int) ///< Get HCI device list
#define HCIGETDEVINFO _IOR('H', 211, int) ///< Get HCI device info

// HCI Channel types
#define HCI_CHANNEL_RAW     0 ///< Raw HCI channel
#define HCI_CHANNEL_USER    1 ///< User HCI channel
#define HCI_CHANNEL_CONTROL 3 ///< Control HCI channel

// HCI device constants
#define HCI_DEV_NONE  0xFFFF  ///< No HCI device
#define HCI_MAX_DEV   16      ///< Maximum number of HCI devices

// L2CAP constants
#define ATT_CID 4 ///< Attribute Protocol CID (Channel Identifier)

// 1 minute in nanoseconds
#define L2_CONNECT_TIMEOUT 60000000000

// HCI Device States Enumeration
enum {
  HCI_UP,       ///< Device is up
  HCI_INIT,     ///< Device is initializing
  HCI_RUNNING,  ///< Device is running

  HCI_PSCAN,    ///< Page scan enabled
  HCI_ISCAN,    ///< Inquiry scan enabled
  HCI_AUTH,     ///< Authentication enabled
  HCI_ENCRYPT,  ///< Encryption enabled
  HCI_INQUIRY,  ///< Inquiry is active

  HCI_RAW       ///< Raw device
};

/**
 * @brief Bluetooth device address structure.
 *
 * Represents a Bluetooth device address in big-endian format.
 * The address is stored in a 6-byte array.
 */
struct __attribute__((packed)) bdaddr_t {
  uint8_t b[6];  ///< Bluetooth device address bytes.

  /**
   * @brief Less-than operator for bdaddr_t.
   *
   * Provides a lexicographical comparison of Bluetooth addresses.
   * Useful for storing bdaddr_t in ordered containers like std::map.
   *
   * @param r Right-hand side bdaddr_t to compare.
   * @return True if this bdaddr_t is less than r.
   */
  bool operator<(const bdaddr_t& r) const {
    for (int i = 0; i < 6; i++) {
      if (b[i] < r.b[i]) return true;
      if (b[i] > r.b[i]) return false;
    }
    return false; // Addresses are equal
  }
};

/**
 * @brief L2CAP socket address structure.
 *
 * Represents an address for L2CAP (Logical Link Control and Adaptation Protocol) sockets.
 */
struct sockaddr_l2 {
  sa_family_t l2_family;      ///< Address family (AF_BLUETOOTH).
  uint16_t    l2_psm;         ///< Protocol/Service Multiplexer (PSM).
  bdaddr_t    l2_bdaddr;      ///< Bluetooth device address.
  uint16_t    l2_cid;         ///< Connection Identifier (CID).
  uint8_t     l2_bdaddr_type; ///< Bluetooth address type (public or random).
};

/**
 * @brief HCI socket address structure.
 *
 * Represents an address for HCI (Host Controller Interface) sockets.
 */
struct sockaddr_hci {
  sa_family_t hci_family;   ///< Address family (AF_BLUETOOTH).
  uint16_t    hci_dev;      ///< HCI device ID.
  uint16_t    hci_channel;  ///< HCI channel.
};

/**
 * @brief HCI device request structure.
 *
 * Used for requesting information about HCI devices.
 */
struct hci_dev_req {
  uint16_t dev_id;  ///< HCI device ID.
  uint32_t dev_opt; ///< Device options.
};

/**
 * @brief HCI filter structure.
 *
 * Used for setting HCI socket filters.
 */
struct hci_filter {
  uint32_t type_mask;     ///< Packet type mask.
  uint32_t event_mask[2]; ///< Event mask array.
  uint16_t opcode;        ///< Opcode filter.
};

/**
 * @brief HCI device list request structure.
 *
 * Used for requesting a list of HCI devices.
 * Contains a flexible array member for device requests.
 */
struct hci_dev_list_req {
  uint16_t         dev_num; ///< Number of devices.
  struct hci_dev_req dev_req[0]; ///< Array of device requests (flexible array member).
};

/**
 * @brief HCI device information structure.
 *
 * Contains detailed information about an HCI device.
 */
struct hci_dev_info {
  uint16_t dev_id;   ///< HCI device ID.
  char     name[8];  ///< HCI device name.

  bdaddr_t bdaddr;   ///< Bluetooth device address.

  uint32_t flags;    ///< Device flags.
  uint8_t  type;     ///< Device type.

  uint8_t  features[8]; ///< Supported features.

  uint32_t pkt_type;    ///< Packet types.
  uint32_t link_policy; ///< Link policy.
  uint32_t link_mode;   ///< Link mode.

  uint16_t acl_mtu;  ///< ACL (Asynchronous Connection-Less) MTU.
  uint16_t acl_pkts; ///< Number of ACL packets.
  uint16_t sco_mtu;  ///< SCO (Synchronous Connection-Oriented) MTU.
  uint16_t sco_pkts; ///< Number of SCO packets.

  // HCI device statistics
  uint32_t err_rx;   ///< Receive errors.
  uint32_t err_tx;   ///< Transmit errors.
  uint32_t cmd_tx;   ///< Commands transmitted.
  uint32_t evt_rx;   ///< Events received.
  uint32_t acl_tx;   ///< ACL data transmitted.
  uint32_t acl_rx;   ///< ACL data received.
  uint32_t sco_tx;   ///< SCO data transmitted.
  uint32_t sco_rx;   ///< SCO data received.
  uint32_t byte_rx;  ///< Bytes received.
  uint32_t byte_tx;  ///< Bytes transmitted.
};

#endif // BLUETOOTH_STRUCTS_H
