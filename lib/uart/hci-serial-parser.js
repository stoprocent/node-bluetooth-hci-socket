const debug = require('debug')('hci-serial-parser');
const { Transform } = require('stream');

const HCI_COMMAND_PKT = 0x01;
const HCI_ACLDATA_PKT = 0x02;
const HCI_EVENT_PKT = 0x04;

class HciSerialParser extends Transform {
  constructor (options) {
    super(options);
    this.reset();
  }

  _transform (chunk, encoding, callback) {
    this.packetData = Buffer.concat([this.packetData, chunk]);
    debug('HciPacketParser._transform:', this.packetData.toString('hex'));

    if (this.packetType === -1 && this.packetData.length > 0) {
      this.packetType = this.packetData.readUInt8(0);
    }

    if (this.listenerCount('raw') > 0) {
      this.emit('raw', this.packetData);
    }

    while (this.packetData.length > 0) {
      if (this.packetSize === 0) {
        if (!this._determinePacketSize()) {
          break;
        }
      }

      if (this.packetData.length < this.prePacketSize + this.packetSize) {
        break;
      }

      const packet = this.packetData.slice(0, this.prePacketSize + this.packetSize);
      this.push(packet);

      this.packetData = this.packetData.slice(this.prePacketSize + this.packetSize);
      this.packetType = -1;
      this.packetSize = 0;
      this.prePacketSize = 0;

      if (this.packetData.length > 0) {
        this.packetType = this.packetData.readUInt8(0);
      } else {
        this.packetType = -1;
      }
    }

    callback();
  }

  _determinePacketSize () {
    if (this.packetData.length < 1) {
      return false;
    }

    const packetType = this.packetData.readUInt8(0);

    if (packetType === HCI_EVENT_PKT || packetType === HCI_COMMAND_PKT) {
      if (this.packetData.length >= 3) {
        this.prePacketSize = 3;
        this.packetSize = this.packetData.readUInt8(2);
        return true;
      }
    } else if (packetType === HCI_ACLDATA_PKT) {
      if (this.packetData.length >= 5) {
        this.prePacketSize = 5;
        this.packetSize = this.packetData.readUInt16LE(3);
        return true;
      }
    } else {
      this.emit('error', new Error(`Unknown packet type: ${packetType}`));
    }
    return false;
  }

  reset () {
    this.packetData = Buffer.alloc(0);
    this.packetType = -1;
    this.packetSize = 0;
    this.prePacketSize = 0;
  }

  _flush (callback) {
    if (this.listenerCount('raw') > 0 && this.packetData.length > 0) {
      this.emit('raw', this.packetData);
    }
    this.reset();
    callback();
  }
}

module.exports = HciSerialParser;
