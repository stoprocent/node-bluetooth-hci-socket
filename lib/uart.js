const EventEmitter = require('events');
const debug = require('debug')('hci-uart');
const { SerialPort } = require('serialport');
const HciSerialParser = require('./uart/hci-serial-parser');
const async = require('async');

const HCI_COMMAND_PKT = 0x01;

const OGF_HOST_CTL = 0x03;
const OCF_RESET = 0x0003;
const OCF_SET_EVENT_FILTER = 0x0005;

const SET_EVENT_FILTER_CMD = OCF_SET_EVENT_FILTER | (OGF_HOST_CTL << 10);
const RESET_CMD = OCF_RESET | (OGF_HOST_CTL << 10);

class BluetoothHciSocket extends EventEmitter {
  constructor (useFilter = false) {
    super();
    this._isUp = false;
    this._useFilter = useFilter;
    this._mode = null;
    this._serialDevice = null;
    this._queue = null;
    this._parser = null;
    this._exitHandler = this.reset.bind(this);
    process.on('exit', this._exitHandler);
  }

  setFilter (filter) {
    if (!this._useFilter) {
      return;
    }

    const header = Buffer.alloc(4);
    header.writeUInt8(HCI_COMMAND_PKT, 0);
    header.writeUInt16LE(SET_EVENT_FILTER_CMD, 1);
    header.writeUInt8(filter.length, 3);

    const cmd = Buffer.concat([header, filter]);
    this.write(cmd);
  }

  bindRaw (devId, params) {
    this.bindUser(devId, params);
    this._mode = 'raw';
    this.reset();
  }

  bindUser (devId, params) {
    this._mode = 'user';
    const uartParams = this._getSerialParams(params);

    const { port, baudRate } = uartParams.uart;
    if (typeof port === 'string' && Number.isInteger(baudRate)) {
      debug(`Using UART PORT = ${port}, BAUD RATE = ${baudRate}`);

      this._serialDevice = new SerialPort({
        path: port,
        baudRate,
        autoOpen: false,
        flowControl: true
      });
    } else {
      throw new Error('Invalid UART parameters');
    }

    if (!this._serialDevice) {
      throw new Error('No compatible UART device found!');
    }

    this._queue = async.queue((data, callback) => {
      this._serialDevice.write(data, callback);
    });
    this._queue.pause();

    this._parser = this._serialDevice.pipe(new HciSerialParser());
    this._parser.on('raw', this.waitForReset.bind(this));

    this._serialDevice.on('error', (error) => this.emit('error', error));
    this._serialDevice.on('close', () => {
      this._isUp = false;
      this.emit('state', this._isUp);
    });

    this._serialDevice.open();
  }

  _getSerialParams (params) {
    let port;
    let baudRate = 1000000; // Default baud rate

    // Check for UART port in environment variables
    if (process.env.BLUETOOTH_HCI_SOCKET_UART_PORT) {
      port = process.env.BLUETOOTH_HCI_SOCKET_UART_PORT;
    }

    // Check for UART baud rate in environment variables
    if (process.env.BLUETOOTH_HCI_SOCKET_UART_BAUDRATE) {
      const parsedBaudRate = parseInt(process.env.BLUETOOTH_HCI_SOCKET_UART_BAUDRATE, 10);
      if (!isNaN(parsedBaudRate)) {
        baudRate = parsedBaudRate;
      }
    }

    // Override with params if provided
    if (params && params.uart) {
      if (params.uart.port && typeof params.uart.port === 'string') {
        port = params.uart.port;
      }
      if (params.uart.baudRate && typeof params.uart.baudRate === 'number' && isFinite(params.uart.baudRate)) {
        baudRate = params.uart.baudRate;
      }
    }

    return { uart: { port, baudRate } };
  }

  bindControl () {
    this._mode = 'control';
  }

  isDevUp () {
    return this._isUp;
  }

  waitForReset (data) {
    const resetPatterns = [
      Buffer.from('040e0401030c00', 'hex'),
      Buffer.from('040e0402030c00', 'hex'),
      Buffer.from('040e0405030c00', 'hex')
    ];

    if (
      !this._isUp &&
      this._mode === 'raw' &&
      resetPatterns.some((pattern) => data.includes(pattern))
    ) {
      debug('Reset complete');
      this._parser.removeAllListeners('raw');
      this._parser.reset();
      this._queue.resume();
      this._isUp = true;
      this.emit('state', this._isUp);
    }
  }

  start () {
    if (this._mode !== 'raw' && this._mode !== 'user') {
      return;
    }
    
    if (!this._serialDevice) {
      throw new Error('Serial device is not initialized');
    }

    this._serialDevice.open();
    this._parser.removeAllListeners('data');
    this._parser.on('data', (data) => {
      if (this._isUp) {
        this.emit('data', data);
      }
    });
  }

  stop () {
    process.removeListener('exit', this._exitHandler);
    if (this._mode !== 'raw' && this._mode !== 'user') {
      return;
    }
    this._parser.removeAllListeners('data');
    if (this._serialDevice.isOpen) {
        this._serialDevice.close();
    }
    this._serialDevice.removeAllListeners();
  }

  write (data) {
    debug(`Write: ${data.toString('hex')}`);
    if (this._mode === 'raw' || this._mode === 'user') {
      this._queue.push(data);
    }
  }

  reset () {
    if (!this._serialDevice) {
      return;
    }

    const cmd = Buffer.alloc(4);
    cmd.writeUInt8(HCI_COMMAND_PKT, 0);
    cmd.writeUInt16LE(RESET_CMD, 1);
    cmd.writeUInt8(0x00, 3);

    debug(`Reset: ${cmd.toString('hex')}`);
    this._serialDevice.write(cmd);
  }
}

module.exports = BluetoothHciSocket;
