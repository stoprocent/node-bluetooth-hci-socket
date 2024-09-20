#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <uv.h>

#include "BluetoothHciL2Socket.h"
#include "BluetoothHciSocket.h"

BluetoothHciL2Socket::BluetoothHciL2Socket(BluetoothHciSocket* parent,
                                           const bdaddr_t* bdaddr_src,
                                           uint8_t src_type,
                                           const bdaddr_t* bdaddr_dst,
                                           uint8_t dst_type,
                                           uint64_t expires)
    : _parent(parent), _expires(expires), _socket(-1)
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

    // Attempt to connect
    this->connect();
}

BluetoothHciL2Socket::~BluetoothHciL2Socket() {
  if(this->_socket != -1) disconnect();
  if(_expires == 0) {
    this->_parent->_l2sockets_connected.erase(_l2_dst.l2_bdaddr);
  }
}

void BluetoothHciL2Socket::connect() {
  _socket = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
  if(_socket < 0) return;

  if (bind(_socket, (struct sockaddr*)&_l2_src, sizeof(_l2_src)) < 0) {
    close(_socket);
    _socket = -1;
    return;
  }

  // the kernel needs to flush the socket before we continue
  while (::connect(_socket, (struct sockaddr *)&_l2_dst, sizeof(_l2_dst)) == -1 ) {
    if(errno == EINTR) {
      continue;
    }
    close(_socket);
    _socket = -1;
    break;
  }
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
