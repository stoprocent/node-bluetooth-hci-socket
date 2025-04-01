const os = require('os');
const platform = os.platform();

function loadDriver (driverType) {
  switch (driverType) {
    case 'uart':
      return require('./lib/uart.js');
    case 'usb':
      return require('./lib/usb.js');
    case 'native':
      return require('./lib/native.js');
    case 'default':
      return getDefaultDriver();
    default:
      return require('./lib/unsupported.js');
  }
}

// Default driver selection logic
function getDefaultDriver () {
  if (process.env.BLUETOOTH_HCI_SOCKET_UART_PORT || process.env.BLUETOOTH_HCI_SOCKET_FORCE_UART) {
    return loadDriver('uart');
  } else if (process.env.BLUETOOTH_HCI_SOCKET_FORCE_USB || platform === 'win32' || platform === 'freebsd') {
    return loadDriver('usb');
  } else if (platform === 'linux' || platform === 'android') {
    return loadDriver('native');
  } else {
    return loadDriver('unsupported');
  }
}

module.exports = loadDriver('default');
module.exports.loadDriver = loadDriver;