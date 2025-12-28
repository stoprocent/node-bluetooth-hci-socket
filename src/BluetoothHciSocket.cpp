#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <uv.h>
#include <stdexcept>

#include "BluetoothHciSocket.h"

BluetoothHciSocket::BluetoothHciSocket(const Napi::CallbackInfo& info) :
  Napi::ObjectWrap<BluetoothHciSocket>(info), 
  stopFlag(false),
  _mode(0),
  _socket(-1),
  _devId(0),
  _address(),
  _addressType(0)
{}

BluetoothHciSocket::~BluetoothHciSocket() {
  if (!stopFlag && pollingThread.joinable()) {
    stopFlag = true;
    pollingThread.join();  // Wait for the polling thread to finishf
  }
  if (this->_socket >= 0) { 
    close(this->_socket); 
    this->_socket = -1; 
  }
}

void BluetoothHciSocket::PollSocket() {
    char buffer[1024];

    while (!stopFlag) {
        int length = read(_socket, buffer, sizeof(buffer));
        if (length > 0) {
            // Handle HCI_CHANNEL_RAW if necessary
            if (this->_mode == HCI_CHANNEL_RAW) {
              this->kernelDisconnectWorkArounds(buffer, length);  // Perform any required workarounds
            }
            
            if (thisObj.IsEmpty()) {
                stopFlag = true;
                break;
            }

            // If we received data, we need to send it to the main JS thread using the ThreadSafeFunction
            std::string message(buffer, length);

            // Use ThreadSafeFunction to safely call the JS function from the background thread
            tsfn.BlockingCall([message, this](Napi::Env env, Napi::Function jsCallback) {
                Napi::HandleScope scope(env);  // Handle scope for managing lifetime of JS objects

                // Prepare arguments for emit event: "data" and the message buffer
                std::vector<napi_value> arguments = {
                    Napi::String::New(env, "data"),  // The event name
                    Napi::Buffer<char>::Copy(env, message.c_str(), message.length())  // The data buffer
                };

                // Call the emit function in JavaScript
                jsCallback.Call(this->thisObj.Value(), arguments);
            });
        } else if (length == 0) {
          continue;
        } else if (stopFlag) {
          break;
        }
    }

    close(_socket);  // Close the socket when done
    tsfn.Release();  // Release the thread-safe function after stopping the thread
}

void BluetoothHciSocket::EmitError(const Napi::CallbackInfo& info, const char *syscall) {
  Napi::Env env = info.Env();  // Get the environment
  Napi::HandleScope scope(env);  // Create a scope for memory management

  // Create an error object with errno and the syscall
  Napi::Error error = Napi::Error::New(env, std::string(strerror(errno)));
  error.Set("syscall", Napi::String::New(env, syscall));
  error.Set("errno", Napi::Number::New(env, errno));

  try {
    // Get the value held by thisObj
    Napi::Value value = info.This().As<Napi::Object>();
    if (!value.IsObject()) {
      throw std::runtime_error("this does not contain a valid object");
    }
    Napi::Object obj = value.As<Napi::Object>();

    // Get the emit function
    Napi::Value emitVal = obj.Get("emit");
    if (!emitVal.IsFunction()) {
      throw std::runtime_error("emit is not a function");
    }
    Napi::Function emitFunc = emitVal.As < Napi::Function > ();

    // Prepare arguments
    std::vector<napi_value> arguments;
    arguments.push_back(Napi::String::New(env, "error"));
    arguments.push_back(error.Value());

    // Call emit
    emitFunc.Call(obj, arguments);
  } catch (const std::exception & ex) {
    Napi::Error::New(env, ex.what()).ThrowAsJavaScriptException();
    return;
  }
}

int BluetoothHciSocket::devIdFor(const int* pDevId, bool isUp) {
  int devId = 0; // default

  if (pDevId == nullptr) {
    struct hci_dev_list_req *dl;
    struct hci_dev_req *dr;

    dl = (hci_dev_list_req*)calloc(HCI_MAX_DEV * sizeof(*dr) + sizeof(*dl), 1);
    dr = dl->dev_req;

    dl->dev_num = HCI_MAX_DEV;

    if (ioctl(this->_socket, HCIGETDEVLIST, dl) > -1) {
      for (int i = 0; i < dl->dev_num; i++, dr++) {
        bool devUp = dr->dev_opt & (1 << HCI_UP);
        bool match = (isUp == devUp);

        if (match) {
          // choose the first device that is match
          // later on, it would be good to also HCIGETDEVINFO and check the HCI_RAW flag
          devId = dr->dev_id;
          break;
        }
      }
    }

    free(dl);
  } else {
    devId = *pDevId;
  }

  return devId;
}

void BluetoothHciSocket::kernelDisconnectWorkArounds(char * data, int length) {
  // Check if it's an HCI Event Packet
  if (length < 4 || static_cast<uint8_t> (data[0]) != HCI_EVENT_PKT) {
    // Not an HCI event packet; nothing to do
    return;
  }

  // 
  std::lock_guard<std::mutex> lock(_mapMutex);

  uint8_t eventCode = static_cast<uint8_t> (data[1]);
  uint8_t plen = static_cast<uint8_t> (data[2]);

  if (eventCode == HCI_EV_LE_META) {
    // Handle LE Meta Events
    if (plen >= 3) {
      uint8_t subEventCode = static_cast<uint8_t> (data[3]);
      uint8_t status = static_cast<uint8_t> (data[4]);

      if ((subEventCode == HCI_EV_LE_CONN_COMPLETE && plen >= 19 && status == HCI_SUCCESS) ||
        (subEventCode == HCI_EV_LE_ENH_CONN_COMPLETE && plen >= 31 && status == HCI_SUCCESS)) {
        // Connection Complete Event
        uint16_t handle = data[5] | (data[6] << 8);
        uint8_t role = static_cast<uint8_t> (data[7]);
        (void)(role);
        
        // Extract the Bluetooth address
        bdaddr_t bdaddr_dst = {};
        if (subEventCode == HCI_EV_LE_CONN_COMPLETE) {
          memcpy(bdaddr_dst.b, & data[9], sizeof(bdaddr_dst.b));
        } else if (subEventCode == HCI_EV_LE_ENH_CONN_COMPLETE) {
          memcpy(bdaddr_dst.b, & data[9], sizeof(bdaddr_dst.b));
        }

        // Process the connection
        std::shared_ptr<BluetoothHciL2Socket> l2socket_ptr;
        auto it_connected = _l2sockets_connected.find(bdaddr_dst);
        if (it_connected != _l2sockets_connected.end()) {
          l2socket_ptr = it_connected->second.lock();
        } else {
          auto it_connecting = _l2sockets_connecting.find(bdaddr_dst);
          if (it_connecting != _l2sockets_connecting.end()) {
            // Successful connection (we have a handle for the socket)
            l2socket_ptr = it_connecting->second;
            l2socket_ptr->setExpires(0);
            _l2sockets_connecting.erase(it_connecting);

            // Move to connected sockets map
            _l2sockets_connected[bdaddr_dst] = std::weak_ptr<BluetoothHciL2Socket> (l2socket_ptr);
          } else {
            // Create bdaddr_t for source address
            bdaddr_t bdaddr_src = {};
            memcpy(bdaddr_src.b, _address, sizeof(bdaddr_src.b));

            // Correct the dst_type calculation
            uint8_t dst_type = static_cast<uint8_t> (data[8] + 1);

            // Create a new L2CAP socket and connect
            l2socket_ptr = std::make_shared<BluetoothHciL2Socket> (
              this, & bdaddr_src, _addressType, & bdaddr_dst, dst_type, 0);

            l2socket_ptr->connect();

            if (!l2socket_ptr->isConnected()) {
              return;
            }

            // Add to connected sockets map
            _l2sockets_connected[bdaddr_dst] = std::weak_ptr<BluetoothHciL2Socket> (l2socket_ptr);
          }
        }

        if (!l2socket_ptr || !l2socket_ptr->isConnected()) {
          return;
        }

        // Map the handle to the L2CAP socket
        handle = handle % 256;
        _l2sockets_handles[handle] = l2socket_ptr;
      }
    }
  } else if (eventCode == HCI_EV_DISCONN_COMPLETE && plen >= 4) {
    uint8_t status = static_cast<uint8_t> (data[3]);
    if (status == HCI_SUCCESS) {
      // Disconnection Complete Event
      uint16_t handle = data[4] | (data[5] << 8);
      handle = handle % 256;

      // Remove the socket associated with the handle
      _l2sockets_handles.erase(handle);
    }
  }
}

void BluetoothHciSocket::setConnectionParameters(
    unsigned short connMinInterval,
    unsigned short connMaxInterval,
    unsigned short connLatency,
    unsigned short supervisionTimeout
){
  char command[128];

  // override the HCI devices connection parameters using debugfs
  sprintf(command, "echo %u > /sys/kernel/debug/bluetooth/hci%d/conn_min_interval", connMinInterval, this->_devId);
  system(command);
  sprintf(command, "echo %u > /sys/kernel/debug/bluetooth/hci%d/conn_max_interval", connMaxInterval, this->_devId);
  system(command);
  sprintf(command, "echo %u > /sys/kernel/debug/bluetooth/hci%d/conn_latency", connLatency, this->_devId);
  system(command);
  sprintf(command, "echo %u > /sys/kernel/debug/bluetooth/hci%d/supervision_timeout", supervisionTimeout, this->_devId);
  system(command);
}

bool BluetoothHciSocket::kernelConnectWorkArounds(char * data, int length) {
  // Check if the packet is an HCI command packet
  if (length < 4 || data[0] != HCI_COMMAND_PKT) {
    // Not an HCI command packet; nothing to do
    return false;
  }

  //
  std::lock_guard<std::mutex> lock(_mapMutex);

  // Extract the opcode and parameter length
  uint16_t opcode = data[1] | (data[2] << 8);
  uint8_t plen = data[3];

  // Variables to hold connection parameters
  uint16_t connMinInterval = 0;
  uint16_t connMaxInterval = 0;
  uint16_t connLatency = 0;
  uint16_t supervisionTimeout = 0;
  bdaddr_t bdaddr_dst = {};
  uint8_t dst_type = 0;
  bool handled = false;

  // Parse the command based on the opcode
  if (opcode == HCI_LE_CREATE_CONN && plen == 0x19) {
    // LE Create Connection command
    // Extract bdaddr and address type
    memcpy(bdaddr_dst.b, & data[10], sizeof(bdaddr_dst.b));
    dst_type = static_cast<uint8_t> (data[9] + 1);

    // Extract connection parameters
    connMinInterval = data[17] | (data[18] << 8);
    connMaxInterval = data[19] | (data[20] << 8);
    connLatency = data[21] | (data[22] << 8);
    supervisionTimeout = data[23] | (data[24] << 8);

    handled = true;
  } else if (opcode == HCI_LE_EXT_CREATE_CONN) {
    // LE Extended Create Connection command
    // Ensure we have enough data
    if (plen >= 0x2A && length >= plen + 4) {
      // Extract bdaddr and address type
      memcpy(bdaddr_dst.b, & data[7], sizeof(bdaddr_dst.b));
      dst_type = static_cast<uint8_t> (data[6] + 1);

      // Extract connection parameters
      connMinInterval = data[18] | (data[19] << 8);
      connMaxInterval = data[20] | (data[21] << 8);
      connLatency = data[22] | (data[23] << 8);
      supervisionTimeout = data[24] | (data[25] << 8);

      handled = true;
    }
  }

  if (handled) {
    // Set the connection parameters
    this->setConnectionParameters(connMinInterval, connMaxInterval, connLatency, supervisionTimeout);

    std::shared_ptr<BluetoothHciL2Socket> l2socket_ptr;

    // Check if the device is already connected
    auto it_connected = this->_l2sockets_connected.find(bdaddr_dst);
    if (it_connected != this->_l2sockets_connected.end()) {
      // Refresh the existing connection
      l2socket_ptr = it_connected->second.lock();
      if (l2socket_ptr) {
        l2socket_ptr->disconnect();
        l2socket_ptr->connect();
        // No expiration needed as we're maintaining the connection
      }
    } else {
      // Check if the device is currently connecting
      auto it_connecting = this->_l2sockets_connecting.find(bdaddr_dst);
      if (it_connecting != this->_l2sockets_connecting.end()) {
        // Reattempt the connection
        l2socket_ptr = it_connecting->second;
        l2socket_ptr->disconnect();
        l2socket_ptr->connect();
        l2socket_ptr->setExpires(uv_hrtime() + L2_CONNECT_TIMEOUT);
      } else {
        // Create a new L2CAP socket and initiate connection
        bdaddr_t bdaddr_src = {};
        memcpy(bdaddr_src.b, _address, sizeof(bdaddr_src.b));

        uint64_t expires = uv_hrtime() + L2_CONNECT_TIMEOUT;

        l2socket_ptr = std::make_shared<BluetoothHciL2Socket> (
          this, & bdaddr_src, _addressType, & bdaddr_dst, dst_type, expires);

        // Insert into the connecting sockets map
        this->_l2sockets_connecting[bdaddr_dst] = l2socket_ptr;

        // Attempt to connect
        l2socket_ptr->connect();

        // Check if connected successfully
        if (!l2socket_ptr->isConnected()) {
          this->_l2sockets_connecting.erase(bdaddr_dst);
          return false;
        }
      }
    }

    // Skip sending the command to the kernel; handled by connect()
    return true;
  }

  // Command not handled; proceed normally
  return false;
}

Napi::Value BluetoothHciSocket::BindRaw(const Napi::CallbackInfo& info) {
  if (!this->EnsureSocket(info)) {
    return info.Env().Undefined();
  }

  Napi::Env env = info.Env();  // Get the environment
  Napi::HandleScope scope(env);  // Create a scope for memory management

  int devId = 0;
  int* pDevId = nullptr;

  // Check if an argument was provided and if it's an integer
  if (info.Length() > 0 && info[0].IsNumber()) {
    devId = info[0].As<Napi::Number>().Int32Value();  // Get the integer value
    pDevId = &devId;
  }

  struct sockaddr_hci a = {};
  struct hci_dev_info di = {};

  memset(&a, 0, sizeof(a));
  a.hci_family = AF_BLUETOOTH;
  a.hci_dev = this->devIdFor(pDevId, true);
  a.hci_channel = HCI_CHANNEL_RAW;

  this->_devId = a.hci_dev;
  this->_mode = HCI_CHANNEL_RAW;

  if (bind(this->_socket, (struct sockaddr *) &a, sizeof(a)) < 0) {
    Napi::Error::New(env, strerror(errno)).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  // get the local address and address type
  memset(&di, 0x00, sizeof(di));
  di.dev_id = this->_devId;
  memset(_address, 0, sizeof(_address));
  _addressType = 0;

  if (ioctl(this->_socket, HCIGETDEVINFO, (void *)&di) > -1) {
    memcpy(_address, &di.bdaddr, sizeof(di.bdaddr));
    _addressType = di.type;

    if (_addressType == 3) {
      // 3 is a weird type, use 1 (public) instead
      _addressType = 1;
    }
  }

  // Return the device ID as a JavaScript number
  return Napi::Number::New(env, this->_devId);
}

Napi::Value BluetoothHciSocket::BindUser(const Napi::CallbackInfo& info) {
  if (!this->EnsureSocket(info)) {
    return info.Env().Undefined();
  }

  Napi::Env env = info.Env();  // Get the environment
  Napi::HandleScope scope(env);  // Create a scope for memory management

  int devId = 0;
  int* pDevId = nullptr;

  // Check if an argument was provided and if it's an integer
  if (info.Length() > 0 && info[0].IsNumber()) {
    devId = info[0].As<Napi::Number>().Int32Value();  // Get the integer value
    pDevId = &devId;
  }

  struct sockaddr_hci a = {};  // Initialize the structure

  // Zero out the memory (this is redundant with the in-line initialization)
  memset(&a, 0, sizeof(a));
  
  // Set the family, device ID, and channel
  a.hci_family = AF_BLUETOOTH;
  a.hci_dev = this->devIdFor(pDevId, false);
  a.hci_channel = HCI_CHANNEL_USER;

  this->_devId = a.hci_dev;    // Set the device ID in the class
  this->_mode = HCI_CHANNEL_USER;  // Set the mode to user channel

  // Perform the bind operation
  if (bind(this->_socket, (struct sockaddr *)&a, sizeof(a)) < 0) {
    // Use Napi for error handling and throw a JS exception
    Napi::Error::New(env, strerror(errno)).ThrowAsJavaScriptException();
    return env.Undefined();
  }

  // Return the device ID as a JavaScript number
  return Napi::Number::New(env, this->_devId);
}

void BluetoothHciSocket::BindControl(const Napi::CallbackInfo& info) {
  if (!this->EnsureSocket(info)) {
    return;
  }

  Napi::Env env = info.Env();  // Get the environment
  Napi::HandleScope scope(env);  // Create a scope for memory management

  struct sockaddr_hci a = {};

  // Zero out the memory (this is also redundant with in-line initialization)
  memset(&a, 0, sizeof(a));

  // Set the family, device ID, and channel
  a.hci_family = AF_BLUETOOTH;
  a.hci_dev = HCI_DEV_NONE;
  a.hci_channel = HCI_CHANNEL_CONTROL;

  // Set the mode to control channel
  this->_mode = HCI_CHANNEL_CONTROL;

  // Perform the bind operation
  if (bind(this->_socket, (struct sockaddr *)&a, sizeof(a)) < 0) {
    // Use Napi for error handling and throw a JS exception
    Napi::Error::New(env, strerror(errno)).ThrowAsJavaScriptException();
  }
}

Napi::Value BluetoothHciSocket::IsDevUp(const Napi::CallbackInfo& info) {
  if (!this->EnsureSocket(info)) {
    return Napi::Boolean::New(info.Env(), false);
  }

  Napi::Env env = info.Env();  // Get the environment
  Napi::HandleScope scope(env);  // Create a scope for memory management

  struct hci_dev_info di = {};
  bool isUp = false;

  memset(&di, 0x00, sizeof(di));
  di.dev_id = this->_devId;

  if (ioctl(this->_socket, HCIGETDEVINFO, (void *)&di) > -1) {
    isUp = (di.flags & (1 << HCI_UP)) != 0;
  }

  return Napi::Boolean::New(env, isUp);
}

Napi::Value BluetoothHciSocket::GetDeviceList(const Napi::CallbackInfo& info) {
  if (!this->EnsureSocket(info)) {
    return Napi::Array::New(info.Env());
  }

  Napi::Env env = info.Env();  // Get the environment
  Napi::HandleScope scope(env);  // Create a scope for memory management

  struct hci_dev_list_req *dl;
  struct hci_dev_req *dr;

  dl = (hci_dev_list_req*)calloc(HCI_MAX_DEV * sizeof(*dr) + sizeof(*dl), 1);
  dr = dl->dev_req;

  dl->dev_num = HCI_MAX_DEV;

  Napi::Array deviceList = Napi::Array::New(env);

  if (ioctl(this->_socket, HCIGETDEVLIST, dl) > -1) {
    int di = 0;
    for (int i = 0; i < dl->dev_num; i++, dr++) {
      uint16_t devId = dr->dev_id;
      bool devUp = dr->dev_opt & (1 << HCI_UP);
      // TODO: smells like there's a bug here (but dr isn't read so...)
      if (dr != nullptr) {
        Napi::Object obj = Napi::Object::New(env);
        obj.Set(Napi::String::New(env, "devId"), Napi::Number::New(env, devId));
        obj.Set(Napi::String::New(env, "devUp"), Napi::Boolean::New(env, devUp));
        obj.Set(Napi::String::New(env, "idVendor"), env.Null());
        obj.Set(Napi::String::New(env, "idProduct"), env.Null());
        obj.Set(Napi::String::New(env, "busNumber"), env.Null());
        obj.Set(Napi::String::New(env, "deviceAddress"), env.Null());
        deviceList.Set(di++, obj);
      }
    }
  }

  free(dl);

  return deviceList;
}

void BluetoothHciSocket::SetFilter(const Napi::CallbackInfo& info) {
  if (!this->EnsureSocket(info)) {
    return;
  }

  Napi::Env env = info.Env();  // Get the environment
  Napi::HandleScope scope(env);  // Create a scope for memory management

  // Check if the first argument exists and is a buffer
  if (info.Length() > 0 && info[0].IsBuffer()) {
    Napi::Buffer<char> buffer = info[0].As<Napi::Buffer<char>>();
    
    // Set the filter using the buffer's data and length
    struct hci_filter filter;
    memset(&filter, 0, sizeof(filter));

    if (buffer.Length() > sizeof(filter)) {
        this->EmitError(info, "setFilter: data length exceeds expected length");
        return;
    }

    memcpy(&filter, buffer.Data(), buffer.Length());

    if (setsockopt(this->_socket, SOL_HCI, HCI_FILTER, &filter, sizeof(filter)) < 0) {
        this->EmitError(info, "setsockopt");
    }
  }
}

void BluetoothHciSocket::Start(const Napi::CallbackInfo& info) {
  if (!stopFlag && pollingThread.joinable()) {
    stopFlag = true;
    pollingThread.join();
  }

  if (!this->EnsureSocket(info)) {
    return;
  }

  Napi::Env env = info.Env();  // Get the environment
  Napi::HandleScope scope(env);  // Create a scope for memory management
  // Store weak reference to the JS object (`info.This()`)
  this->thisObj = Reference<Napi::Object>::New(info.This().As<Napi::Object>());

  // Create a thread-safe function for safely calling JS from a background thread
  this->tsfn = Napi::ThreadSafeFunction::New(
    env,
    thisObj.Value().Get("emit").As<Napi::Function>(),  // JavaScript `emit` function
    "Socket Polling",     // Resource name for debugging
    0,                    // Unlimited queue
    1                     // Only one thread will use this tsfn
  );

  // Reset stop flag
  stopFlag = false;
  // Start the polling thread
  pollingThread = std::thread(&BluetoothHciSocket::PollSocket, this);
}

void BluetoothHciSocket::Stop(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();  // Get the environment
  Napi::HandleScope scope(env);  // Create a scope for memory management
  if (!stopFlag && pollingThread.joinable()) {
    stopFlag = true;
    pollingThread.join();  // Wait for the polling thread to finishf
  }
}

void BluetoothHciSocket::Write(const Napi::CallbackInfo& info) {
  if (!this->EnsureSocket(info)) {
    return;
  }

  Napi::Env env = info.Env();  // Get the environment
  Napi::HandleScope scope(env);  // Create a scope for memory management
  
  // Check if the first argument is provided and is a buffer
  if (info.Length() > 0 && info[0].IsBuffer()) {
    Napi::Buffer<char> buffer = info[0].As<Napi::Buffer<char>>();
    
    // Write the data using the buffer's data and length
    if (this->_mode == HCI_CHANNEL_RAW && this->kernelConnectWorkArounds(buffer.Data(), buffer.Length())) {
      return;
    }

    if (write(this->_socket, buffer.Data(), buffer.Length()) < 0) {
      this->EmitError(info, "write");
    }
  }
}

void BluetoothHciSocket::Cleanup(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();  // Get the current efnvironment
  Napi::HandleScope scope(env);  // Create a handle scope for memory management

  auto now = uv_hrtime();

  for (auto it = this->_l2sockets_connecting.cbegin(); it != this->_l2sockets_connecting.cend() /* not hoisted */; /* no increment */) {
    if (now < it->second->getExpires()) {
      this->_l2sockets_connecting.erase(it++);    // or "it = m.erase(it)" since C++11
    } else {
      ++it;
    }
  }
}

bool BluetoothHciSocket::EnsureSocket(const Napi::CallbackInfo& info) {
  if (this->_socket >= 0) {
    return true;
  }

  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  
  int fd = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI);
  if (fd == -1) {
    this->EmitError(info, "socket creation failed");
    return false;
  }

  struct timeval tv = {};
  
  memset(&tv, 0, sizeof(tv));
  tv.tv_sec = 1;
  tv.tv_usec = 0;
  
  // Allow the poll thread to check the stop flag periodically even when no data is incoming
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    this->EmitError(info, "setsockopt failed for SO_RCVTIMEO");
  }
  
  this->_socket = fd;
  return true;
}

Napi::Object BluetoothHciSocket::Init(Napi::Env env, Napi::Object exports) {
  Napi::HandleScope scope(env);

  // Create the function for the class
  Napi::Function func = DefineClass(env, "BluetoothHciSocket", {
    InstanceMethod("start", &BluetoothHciSocket::Start),
    InstanceMethod("bindRaw", &BluetoothHciSocket::BindRaw),
    InstanceMethod("bindUser", &BluetoothHciSocket::BindUser),
    InstanceMethod("bindControl", &BluetoothHciSocket::BindControl),
    InstanceMethod("isDevUp", &BluetoothHciSocket::IsDevUp),
    InstanceMethod("getDeviceList", &BluetoothHciSocket::GetDeviceList),
    InstanceMethod("setFilter", &BluetoothHciSocket::SetFilter),
    InstanceMethod("stop", &BluetoothHciSocket::Stop),
    InstanceMethod("write", &BluetoothHciSocket::Write),
    InstanceMethod("cleanup", &BluetoothHciSocket::Cleanup)
  });

  Napi::FunctionReference* constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(func);
  env.SetInstanceData(constructor);

  exports.Set("BluetoothHciSocket", func);
  return exports;
}

NODE_API_NAMED_ADDON(addon, BluetoothHciSocket);
