const events = require('events');
const { resolve } = require('path');
const dir = resolve(__dirname, '..');
const { BluetoothHciSocket } = require('node-gyp-build')(dir);

inherits(BluetoothHciSocket, events.EventEmitter);

const HCI_COMMAND_PKT = 0x01;
const OGF_HOST_CTL = 0x03;
const OCF_RESET = 0x0003;

class BluetoothHciSocketWrapped extends BluetoothHciSocket {
  start () {
    if (this._timer) {
      clearInterval(this._timer);
    }
    // Every minute perform a cleanup of connecting devices
    this._timer = setInterval(() => {
      this.cleanup();
    }, 60 * 1000);
    this._timer.unref();
    return super.start();
  }

  stop () {
    clearInterval(this._timer);
    return super.stop();
  }

  reset () {
    const cmd = Buffer.alloc(4);

    // header
    cmd.writeUInt8(HCI_COMMAND_PKT, 0);
    cmd.writeUInt16LE(OCF_RESET | OGF_HOST_CTL << 10, 1);
  
    // length
    cmd.writeUInt8(0x00, 3);
  
    debug('reset');
    this.write(cmd);
  }
}

// extend prototype
function inherits (target, source) {
  for (const k in source.prototype) {
    target.prototype[k] = source.prototype[k];
  }
}

module.exports = BluetoothHciSocketWrapped;
