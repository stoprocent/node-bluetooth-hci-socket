const events = require('events');
const util = require('util');

const debug = require('debug')('hci-uart');
const { SerialPort } = require('serialport');
const HciSerialParser = require('./uart/hci-serial-parser');
const async = require('async');

const HCI_COMMAND_PKT = 0x01;
// eslint-disable-next-line no-unused-vars
const HCI_ACLDATA_PKT = 0x02;
// eslint-disable-next-line no-unused-vars
const HCI_EVENT_PKT = 0x04;

const OGF_HOST_CTL = 0x03;
const OCF_RESET = 0x0003;

const OCF_SET_EVENT_FILTER = 0x0005;
const SET_EVENT_FILTER_CMD = OCF_SET_EVENT_FILTER | OGF_HOST_CTL << 10;

const RESET_CMD = OCF_RESET | (OGF_HOST_CTL << 10);

function BluetoothHciSocket () {
  this._isUp = false;
}

util.inherits(BluetoothHciSocket, events.EventEmitter);

BluetoothHciSocket.prototype.setFilter = function (filter) {
  // Filter Header
  let cmd = Buffer.alloc(4);
  cmd.writeUInt8(HCI_COMMAND_PKT, 0);
  cmd.writeUInt16LE(SET_EVENT_FILTER_CMD, 1);

  // Length
  cmd.writeUInt8(filter.length, 3);

  // Concate Buffers
  cmd = Buffer.concat([cmd, filter]);

  this.write(cmd);
};

BluetoothHciSocket.prototype.bindRaw = function (devId, params) {
  this.bindUser(devId, params);

  this._mode = 'raw';

  this.reset();
};

BluetoothHciSocket.prototype.bindUser = function (devId, params) {
  this._mode = 'user';

  const uartParams = this._getSerialParams(params);

  if ((typeof uartParams.uart.port === 'string' || uartParams.uart.port instanceof String) && Number.isInteger(uartParams.uart.baudRate)) {
    debug('using UART PORT = ' + uartParams.uart.port + ', BAUD RATE = ' + uartParams.uart.baudRate);

    this._serialDevice = new SerialPort({
      path: uartParams.uart.port,
      baudRate: uartParams.uart.baudRate,
      autoOpen: false,
      flowControl: true
    });
  }

  if (!this._serialDevice) {
    throw new Error('No compatible UART device found!');
  }

  // Message Queue
  this._queue = async.queue((data, callback) => this._serialDevice.write(data, (a, b) => callback()));
  this._queue.pause();

  // Serial Port Parser
  this._parser = this._serialDevice.pipe(new HciSerialParser());

  // Serial Port Event Listeners
  this._serialDevice.on('error', error => this.emit('error', error));
  this._serialDevice.on('close', () => { this.isDevUp = false; });

  // Open Serial Port to begin
  this._serialDevice.open();
};

BluetoothHciSocket.prototype._getSerialParams = function (params) {
  const uartParams = {
    uart: {
      port: undefined, baudRate: 1000000
    }
  };

  if (process.env.BLUETOOTH_HCI_SOCKET_UART_PORT) {
    uartParams.uart.port = process.env.BLUETOOTH_HCI_SOCKET_UART_PORT;
  }
  if (process.env.BLUETOOTH_HCI_SOCKET_UART_BAUDRATE) {
    uartParams.uart.baudRate = parseInt(process.env.BLUETOOTH_HCI_SOCKET_UART_BAUDRATE, 10);
  }

  if (params && params.uart) {
    if (params.uart.port instanceof String || typeof params.uart.port === 'string') {
      uartParams.uart.port = params.uart.port;
    }
    if (Number.isInteger(params.uart.baudRate)) {
      uartParams.uart.baudRate = params.uart.baudRate;
    }
  }

  return uartParams;
};

BluetoothHciSocket.prototype.bindControl = function () {
  this._mode = 'control';
};

BluetoothHciSocket.prototype.isDevUp = function () {
  return this._isUp;
};

BluetoothHciSocket.prototype.start = function () {
  if (this._mode === 'raw' || this._mode === 'user') {
    this._parser.on('data', (data) => {
      // Handle Reset
      if (this._mode === 'raw' && data.length === 7 && (data.toString('hex') === '040e0401030c00' || data.toString('hex') === '040e0402030c00')) {
        debug('reset complete');
        this._queue.resume();
        this._isUp = true;
      }

      // Emit Data Event on HCI
      this.emit('data', data);
    });
  }
};

BluetoothHciSocket.prototype.stop = function () {
  if (this._mode === 'raw' || this._mode === 'user') {
    this._parser.removeAllListeners('data');
  }
};

BluetoothHciSocket.prototype.write = function (data) {
  debug('write: ' + data.toString('hex'));

  if (this._mode === 'raw' || this._mode === 'user') {
    this._queue.push(data);
  }
};

BluetoothHciSocket.prototype.reset = function () {
  const cmd = Buffer.alloc(4);

  // header
  cmd.writeUInt8(HCI_COMMAND_PKT, 0);
  cmd.writeUInt16LE(RESET_CMD, 1);

  // length
  cmd.writeUInt8(0x00, 3);

  debug('reset:', cmd.toString('hex'));
  this._serialDevice.write(cmd);
};

module.exports = BluetoothHciSocket;
