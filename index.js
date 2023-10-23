const os = require('os');
const platform = os.platform();

if (process.env.BLUETOOTH_HCI_SOCKET_UART_PORT || process.env.BLUETOOTH_HCI_SOCKET_FORCE_UART) {
  module.exports = require('./lib/uart.js');
} else if (process.env.BLUETOOTH_HCI_SOCKET_FORCE_USB || platform === 'win32' || platform === 'freebsd') {
  module.exports = require('./lib/usb.js');
} else if (platform === 'linux' || platform === 'android') {
  module.exports = require('./lib/native');
} else {
  module.exports = require('./lib/unsupported');
}
