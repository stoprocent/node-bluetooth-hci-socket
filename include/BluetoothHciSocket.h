#ifndef BLUETOOTH_HCI_SOCKET_H
#define BLUETOOTH_HCI_SOCKET_H

// Include necessary headers
#include <napi.h>         // N-API for Node.js addons

#include <atomic>         // For std::atomic
#include <thread>         // For std::thread
#include <map>            // For std::map
#include <memory>         // For smart pointers
#include "BluetoothHciL2Socket.h" // Header for BluetoothHciL2Socket class

/**
 * @brief Class representing a Bluetooth HCI (Host Controller Interface) socket.
 *
 * This class interfaces with the Bluetooth hardware using HCI sockets
 * and integrates with Node.js using N-API for addon development.
 */
class BluetoothHciSocket : public Napi::ObjectWrap<BluetoothHciSocket> {
  friend class BluetoothHciL2Socket; ///< Grant access to BluetoothHciL2Socket class

 public:
  /**
   * @brief Initializes the BluetoothHciSocket class and sets up exports to Node.js.
   * @param env The N-API environment.
   * @param exports The exports object to which the class is added.
   * @return The modified exports object.
   */
  static Napi::Object Init(Napi::Env env, Napi::Object exports);

  /**
   * @brief Constructor for BluetoothHciSocket.
   * @param info Callback information from N-API.
   */
  BluetoothHciSocket(const Napi::CallbackInfo& info);

  /// Destructor
  ~BluetoothHciSocket();

  // Binding methods to interface with HCI socket
  /**
   * @brief Binds the socket in raw mode.
   * @param info Callback information from N-API.
   * @return Napi::Value indicating success or failure.
   */
  Napi::Value BindRaw(const Napi::CallbackInfo& info);

  /**
   * @brief Binds the socket in user mode.
   * @param info Callback information from N-API.
   * @return Napi::Value indicating success or failure.
   */
  Napi::Value BindUser(const Napi::CallbackInfo& info);

  /**
   * @brief Binds the socket in control mode.
   * @param info Callback information from N-API.
   */
  void BindControl(const Napi::CallbackInfo& info);

  // Device methods
  /**
   * @brief Checks if the Bluetooth device is up.
   * @param info Callback information from N-API.
   * @return Napi::Value indicating the device status.
   */
  Napi::Value IsDevUp(const Napi::CallbackInfo& info);

  /**
   * @brief Retrieves the list of Bluetooth devices.
   * @param info Callback information from N-API.
   * @return Napi::Value containing the device list.
   */
  Napi::Value GetDeviceList(const Napi::CallbackInfo& info);

  // Configuration methods
  /**
   * @brief Sets the HCI filter for the socket.
   * @param info Callback information from N-API.
   */
  void SetFilter(const Napi::CallbackInfo& info);

  // Control methods
  /**
   * @brief Starts the socket for communication.
   * @param info Callback information from N-API.
   */
  void Start(const Napi::CallbackInfo& info);

  /**
   * @brief Stops the socket communication.
   * @param info Callback information from N-API.
   */
  void Stop(const Napi::CallbackInfo& info);

  /**
   * @brief Writes data to the socket.
   * @param info Callback information from N-API.
   */
  void Write(const Napi::CallbackInfo& info);

  /**
   * @brief Cleans up resources used by the socket.
   * @param info Callback information from N-API.
   */
  void Cleanup(const Napi::CallbackInfo& info);

 private:
  /**
   * @brief Polls the socket for events in a separate thread.
   */
  void PollSocket();

  /**
   * @brief Emits an error event based on errno.
   * @param info Callback information from N-API.
   * @param syscall Name of the system call that failed.
   */
  void EmitError(const Napi::CallbackInfo& info, const char* syscall);

  /**
   * @brief Retrieves the device ID for a given device.
   * @param devId Pointer to the device ID.
   * @param isUp Whether to check if the device is up.
   * @return Device ID or -1 on failure.
   */
  int devIdFor(const int* devId, bool isUp);

  /**
   * @brief Workaround for kernel disconnect issues.
   * @param length Length of the data.
   * @param data Pointer to the data buffer.
   * @return Result code.
   */
  int kernelDisconnectWorkArounds(int length, char* data);

  /**
   * @brief Workaround for kernel connect issues.
   * @param data Pointer to the data buffer.
   * @param length Length of the data.
   * @return True if handled, false otherwise.
   */
  bool kernelConnectWorkArounds(char* data, int length);

  /**
   * @brief Sets connection parameters for Bluetooth LE connections.
   * @param connMinInterval Minimum connection interval.
   * @param connMaxInterval Maximum connection interval.
   * @param connLatency Connection latency.
   * @param supervisionTimeout Supervision timeout.
   */
  void setConnectionParameters(uint16_t connMinInterval, uint16_t connMaxInterval, uint16_t connLatency, uint16_t supervisionTimeout);

  // N-API thread-safe function and object reference
  Napi::ThreadSafeFunction tsfn;  ///< Thread-safe function for callbacks
  Napi::ObjectReference thisObj;  ///< Reference to the JavaScript object

  // Threading and synchronization
  std::atomic<bool> stopFlag;     ///< Atomic flag to signal the polling thread to stop
  std::thread pollingThread;      ///< Thread for polling the socket

  // Internal state
  int _mode;                  ///< Operating mode of the socket
  int _socket;                ///< File descriptor for the socket
  int _devId;                 ///< Device ID

  uint8_t _address[6];        ///< Local Bluetooth device address
  uint8_t _addressType;       ///< Address type (public or random)

  // Maps to manage connected and connecting L2CAP sockets
  std::map<bdaddr_t, std::weak_ptr<BluetoothHciL2Socket>> _l2sockets_connected;    ///< Connected L2CAP sockets
  std::map<bdaddr_t, std::shared_ptr<BluetoothHciL2Socket>> _l2sockets_connecting; ///< Connecting L2CAP sockets
  std::map<uint16_t, std::shared_ptr<BluetoothHciL2Socket>> _l2sockets_handles;    ///< L2CAP sockets by handle
};

#endif // BLUETOOTH_HCI_SOCKET_H
