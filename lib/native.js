const events = require('events');
const { resolve } = require('path');
const dir = resolve(__dirname, '..');
const { BluetoothHciSocket } = require('node-gyp-build')(dir);

inherits(BluetoothHciSocket, events.EventEmitter);

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
}

// extend prototype
function inherits (target, source) {
  for (const k in source.prototype) {
    target.prototype[k] = source.prototype[k];
  }
}

module.exports = BluetoothHciSocketWrapped;
