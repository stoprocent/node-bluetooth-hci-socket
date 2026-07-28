#include <errno.h>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdexcept>

#include "BluetoothHciL2Socket.h"
#include "BluetoothHciSocket.h"

namespace {
constexpr int L2CAP_CONNECT_TIMEOUT_MS_DEFAULT = 2000;

int getL2capConnectTimeoutMs() {
  const char* timeoutEnv = std::getenv("BLUETOOTH_HCI_L2CAP_CONNECT_TIMEOUT_MS");
  if (timeoutEnv == nullptr) return L2CAP_CONNECT_TIMEOUT_MS_DEFAULT;

  errno = 0;
  char* end = nullptr;
  const long timeoutMs = std::strtol(timeoutEnv, &end, 10);
  if (errno == ERANGE || end == timeoutEnv || *end != '\0' ||
      timeoutMs < 0 || timeoutMs > INT_MAX) {
    return L2CAP_CONNECT_TIMEOUT_MS_DEFAULT;
  }

  return static_cast<int>(timeoutMs);
}

int waitForConnect(int socket, int timeoutMs) {
  struct pollfd descriptor = { socket, POLLOUT, 0 };

  if (timeoutMs == 0) {
    int pollResult;
    do {
      pollResult = poll(&descriptor, 1, -1);
    } while (pollResult < 0 && errno == EINTR);
    return pollResult;
  }

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs);
  int remainingMs = timeoutMs;

  while (true) {
    const int pollResult = poll(&descriptor, 1, remainingMs);
    if (pollResult >= 0 || errno != EINTR) return pollResult;

    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) return 0;
    remainingMs = static_cast<int>(remaining);
  }
}
}

BluetoothHciL2Socket::BluetoothHciL2Socket(BluetoothHciSocket* parent,
                                           const bdaddr_t* bdaddr_src,
                                           uint8_t src_type,
                                           const bdaddr_t* bdaddr_dst,
                                           uint8_t dst_type,
                                           uint64_t expires)
    : _socket(-1), _parent(parent), _expires(expires)
{
    uint16_t l2cid;

    // Convert ATT_CID to Bluetooth byte order
#if __BYTE_ORDER == __LITTLE_ENDIAN
    l2cid = ATT_CID;
#elif __BYTE_ORDER == __BIG_ENDIAN
    l2cid = bswap_16(ATT_CID);
#else
    #error "Unknown byte order"
#endif

    // Initialize source L2CAP address
    memset(&_l2_src, 0, sizeof(_l2_src));
    _l2_src.l2_family = AF_BLUETOOTH;
    _l2_src.l2_cid = l2cid;
    memcpy(&_l2_src.l2_bdaddr, bdaddr_src, sizeof(bdaddr_t));
    _l2_src.l2_bdaddr_type = src_type;

    // Initialize destination L2CAP address
    memset(&_l2_dst, 0, sizeof(_l2_dst));
    _l2_dst.l2_family = AF_BLUETOOTH;
    _l2_dst.l2_cid = l2cid;
    memcpy(&_l2_dst.l2_bdaddr, bdaddr_dst, sizeof(bdaddr_t));
    _l2_dst.l2_bdaddr_type = dst_type; // BDADDR_LE_PUBLIC (0x01), BDADDR_LE_RANDOM (0x02)
}

BluetoothHciL2Socket::~BluetoothHciL2Socket() {
  if(this->_socket != -1) disconnect();
  if(_expires == 0) {
    this->_parent->_l2sockets_connected.erase(_l2_dst.l2_bdaddr);
  }
}

BluetoothHciL2ConnectResult BluetoothHciL2Socket::connect() {
  this->_socket = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
  if(this->_socket < 0) return BluetoothHciL2ConnectResult::SETUP_FAILED;

  if (bind(this->_socket, (struct sockaddr*)&_l2_src, sizeof(_l2_src)) < 0) {
    close(this->_socket);
    this->_socket = -1;
    return BluetoothHciL2ConnectResult::SETUP_FAILED;
  }

  const int socketFlags = fcntl(this->_socket, F_GETFL, 0);
  if (socketFlags < 0 || fcntl(this->_socket, F_SETFL, socketFlags | O_NONBLOCK) < 0) {
    close(this->_socket);
    this->_socket = -1;
    return BluetoothHciL2ConnectResult::SETUP_FAILED;
  }

  if (::connect(this->_socket, (struct sockaddr *)&_l2_dst, sizeof(_l2_dst)) == 0) {
    fcntl(this->_socket, F_SETFL, socketFlags);
    return BluetoothHciL2ConnectResult::CONNECTED;
  }

  if (errno != EINPROGRESS) {
    close(this->_socket);
    this->_socket = -1;
    return BluetoothHciL2ConnectResult::SETUP_FAILED;
  }

  // EINPROGRESS confirms that the kernel accepted the request and initiated
  // the controller connection. Bound the synchronous wait because connect()
  // runs on the thread driving libuv. A timeout closes the kernel socket before
  // allowing the caller to fall back to the original raw HCI command.
  const int pollResult = waitForConnect(this->_socket, getL2capConnectTimeoutMs());

  if (pollResult == 0) {
    close(this->_socket);
    this->_socket = -1;
    return BluetoothHciL2ConnectResult::CONNECTION_TIMED_OUT;
  }

  int connectError = 0;
  socklen_t connectErrorLength = sizeof(connectError);
  if (pollResult < 0 ||
      getsockopt(this->_socket, SOL_SOCKET, SO_ERROR, &connectError, &connectErrorLength) < 0 ||
      connectError != 0) {
    close(this->_socket);
    this->_socket = -1;
    return BluetoothHciL2ConnectResult::CONNECTION_FAILED;
  }

  fcntl(this->_socket, F_SETFL, socketFlags);
  return BluetoothHciL2ConnectResult::CONNECTED;
}

void BluetoothHciL2Socket::disconnect() {
  if(this->_socket != -1) close(this->_socket);
  this->_socket = -1;
}

void BluetoothHciL2Socket::setExpires(uint64_t expires){
  _expires = expires;
}

uint64_t BluetoothHciL2Socket::getExpires() const {
  return _expires;
}

bool BluetoothHciL2Socket::isConnected() const {
  return this->_socket != -1;
}
