#ifndef BLUETOOTH_HCI_L2_SOCKET_H
#define BLUETOOTH_HCI_L2_SOCKET_H

// Forward declaration of BluetoothHciSocket
class BluetoothHciSocket;

// Include necessary headers
#include "BluetoothStructs.h"

// Bluetooth HCI L2CAP Socket class
class BluetoothHciL2Socket {
 public:
  /**
   * @brief Constructor for BluetoothHciL2Socket.
   * @param parent Pointer to the parent Bluetooth HCI socket.
   * @param bdaddr_src Source Bluetooth device address.
   * @param src_type Source address type (public or random).
   * @param bdaddr_dst Destination Bluetooth device address.
   * @param dst_type Destination address type (public or random).
   * @param expires Expiration time in milliseconds.
   */
  BluetoothHciL2Socket(BluetoothHciSocket* parent,
                       const bdaddr_t* bdaddr_src,
                       uint8_t src_type,
                       const bdaddr_t* bdaddr_dst,
                       uint8_t dst_type,
                       uint64_t expires);

  /// Destructor
  ~BluetoothHciL2Socket();

  /// Connects to the remote device.
  void connect();

  /// Disconnects the socket.
  void disconnect();

  /// Sets the expiration time.
  void setExpires(uint64_t expires);

  /// Retrieves the expiration time.
  uint64_t getExpires() const;

  /// Checks if the socket is connected.
  bool isConnected() const;

 private:
  int _socket;                       ///< Socket file descriptor.
  BluetoothHciSocket* _parent;       ///< Pointer to the parent HCI socket.
  uint64_t _expires;                 ///< Expiration time in milliseconds, or 0 if connected.
  struct sockaddr_l2 _l2_src;        ///< Source L2CAP address.
  struct sockaddr_l2 _l2_dst;        ///< Destination L2CAP address.

  // Disable copy constructor and assignment operator
  BluetoothHciL2Socket(const BluetoothHciL2Socket&) = delete;
  BluetoothHciL2Socket& operator=(const BluetoothHciL2Socket&) = delete;
};

#endif // BLUETOOTH_HCI_L2_SOCKET_H
