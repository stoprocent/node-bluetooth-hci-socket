#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <uv.h>

#include "BluetoothHciSocket.h"

BluetoothHciSocket::BluetoothHciSocket(const Napi::CallbackInfo& info) :
  Napi::ObjectWrap<BluetoothHciSocket>(info), 
  stopFlag(false),
  _mode(0),
  _socket(-1),
  _devId(0),
  _address(),
  _addressType(0)
{
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);
  int fd = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI);
  if (fd == -1) {
    Napi::Error::New(env, "socket creation failed").ThrowAsJavaScriptException();
    return;
  }
  this->_socket = fd;
}

BluetoothHciSocket::~BluetoothHciSocket() {
  stopFlag = true;
  if (pollingThread.joinable()) {
      pollingThread.join();  // Wait for the polling thread to finish
  }
  if (tsfn != nullptr) {
    tsfn.Release();
  }
  close(this->_socket);
}

void BluetoothHciSocket::PollSocket() {
    char buffer[1024];

    while (!stopFlag) {
        int length = read(_socket, buffer, sizeof(buffer));
        if (length > 0) {
            // Handle HCI_CHANNEL_RAW if necessary
            if (this->_mode == HCI_CHANNEL_RAW) {
              this->kernelDisconnectWorkArounds(length, buffer);  // Perform any required workarounds
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

  std::vector<napi_value> arguments = {
      Napi::String::New(env, "error"),  // The event name
      error.Value() // The error object
  };

  // Emit the error event with the error object
  this->thisObj.Value().Get("emit").As<Napi::Function>().Call(this->thisObj.Value(), arguments);
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

int BluetoothHciSocket::kernelDisconnectWorkArounds(int length, char* data) {
  // HCI Event - LE Meta Event - LE Connection Complete => manually create L2CAP socket to force kernel to book keep
  // this socket will be closed immediately.

  // The if statement:
  // data[0] = LE Meta Event (HCI_EVENT_PKT)
  // data[1] = HCI_EV_LE_META
  // data[2] = plen (0x13)
  // data[3] = HCI_EV_LE_CONN_COMPLETE (0x01)
  // data[4] = Status (0x00 = Success)
  // data[5,6] = handle (little endian)
  // data[7] = role (0x00 = Master)
  // data[9,]  = device bt address
  if (length == 22 && data[0] == 0x04 && data[1] == 0x3e && data[2] == 0x13 && data[3] == 0x01 && data[4] == 0x00) { //  && data[7] == 0x01
    unsigned short handle = *((unsigned short*)(&data[5]));

    std::shared_ptr<BluetoothHciL2Socket> l2socket_ptr;

    auto it = _l2sockets_connected.find(*(bdaddr_t*)&data[9]);
    if(it != _l2sockets_connected.end()){
      l2socket_ptr = it->second.lock();
    } else {
      auto it2 = _l2sockets_connecting.find(*(bdaddr_t*)&data[9]);

      if(it2 != _l2sockets_connecting.end()){
        //successful connection (we have a handle for the socket!)
        l2socket_ptr = it2->second;
        l2socket_ptr->setExpires(0);
        _l2sockets_connecting.erase(it2);
      } else {

        // Create bdaddr_t for source address
        bdaddr_t bdaddr_src;
        memcpy(bdaddr_src.b, _address, sizeof(bdaddr_src.b));

        // Create bdaddr_t for destination address
        bdaddr_t bdaddr_dst;
        memcpy(bdaddr_dst.b, &data[9], sizeof(bdaddr_dst.b));

        // Correct the dst_type calculation if necessary
        uint8_t dst_type = static_cast<uint8_t>(data[8] + 1);

        // Now call the constructor with proper types
        l2socket_ptr = std::make_shared<BluetoothHciL2Socket>(this, &bdaddr_src, _addressType, &bdaddr_dst, dst_type, static_cast<uint64_t>(0));

        if(!l2socket_ptr->isConnected()){
          return 0;
        }
        this->_l2sockets_connected[*(bdaddr_t*)&data[9]] = l2socket_ptr;
      }
    }

    if(!l2socket_ptr->isConnected()){
      return 0;
    }
    
    handle = handle % 256;
    this->_l2sockets_handles[handle] = l2socket_ptr;
  } else if (length == 7 && data[0] == 0x04 && data[1] == 0x05 && data[2] == 0x04 && data[3] == 0x00) {
    
    // HCI Event - Disconn Complete =======================> close socket from above
    unsigned short handle = *((unsigned short*)(&data[4]));
    handle = handle % 256;
    this->_l2sockets_handles.erase(handle);
  }

  return 0;
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

bool BluetoothHciSocket::kernelConnectWorkArounds(char* data, int length)
{
  // if statement:
  // data[0]: HCI_COMMAND_PKT
  // data[1,2]: HCI_OP_LE_CREATE_CONN (0x200d)
  // data[3]: plen
  // data[10 ...] bdaddr

  if (length == 29 && data[0] == 0x01 && data[1] == 0x0d && data[2] == 0x20 && data[3] == 0x19) {
    unsigned short connMinInterval;
    unsigned short connMaxInterval;
    unsigned short connLatency;
    unsigned short supervisionTimeout;

    // extract the connection parameter
    connMinInterval = (data[18] << 8) | data[17];
    connMaxInterval = (data[20] << 8) | data[19];
    connLatency = (data[22] << 8) | data[21];
    supervisionTimeout = (data[24] << 8) | data[23];

    this->setConnectionParameters(connMinInterval, connMaxInterval, connLatency, supervisionTimeout);

    std::shared_ptr<BluetoothHciL2Socket> l2socket_ptr;
    if(this->_l2sockets_connected.find(*(bdaddr_t*)&data[10]) != this->_l2sockets_connected.end()){
      // we are refreshing the connection (which was connected)
      l2socket_ptr = this->_l2sockets_connected[*(bdaddr_t*)&data[10]].lock();
      l2socket_ptr->disconnect();
      l2socket_ptr->connect();
      // no expiration as we will continue to be "connected" on the other handle which must exist
    } else if(this->_l2sockets_connecting.find(*(bdaddr_t*)&data[10]) != this->_l2sockets_connecting.end()){
      // we were connecting but now we connect again
      l2socket_ptr = this->_l2sockets_connecting[*(bdaddr_t*)&data[10]];
      l2socket_ptr->disconnect();
      l2socket_ptr->connect();
      l2socket_ptr->setExpires(uv_hrtime() + L2_CONNECT_TIMEOUT);
    } else{    
      // Create bdaddr_t for source address
      bdaddr_t bdaddr_src;
      memcpy(bdaddr_src.b, _address, sizeof(bdaddr_src.b));

      // Create bdaddr_t for destination address
      bdaddr_t bdaddr_dst;
      memcpy(bdaddr_dst.b, &data[10], sizeof(bdaddr_dst.b));

      // Correct the dst_type calculation if necessary
      uint8_t dst_type = static_cast<uint8_t>(data[9] + 1);
      
      // Expires
      uint64_t expires = static_cast<uint64_t>(uv_hrtime() + L2_CONNECT_TIMEOUT);

      // Now call the constructor with proper types
      l2socket_ptr = std::make_shared<BluetoothHciL2Socket>(this, &bdaddr_src, _addressType, &bdaddr_dst, dst_type, expires);
      if(!l2socket_ptr->isConnected()){
        return false;
      }
      this->_l2sockets_connecting[*(bdaddr_t*)&data[10]] = l2socket_ptr;
    }

    // returns true to skip sending the kernel this commoand
    // the command will instead be sent by the connect() operation
    return true;
  }

  return false;
}

Napi::Value BluetoothHciSocket::BindRaw(const Napi::CallbackInfo& info) {
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
  
  stopFlag = true;
  if (pollingThread.joinable()) {
      pollingThread.join();  // Wait for the polling thread to finish
  }
}

void BluetoothHciSocket::Write(const Napi::CallbackInfo& info) {
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
  Napi::Env env = info.Env();  // Get the current environment
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
