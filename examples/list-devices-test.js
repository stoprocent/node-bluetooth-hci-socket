const { loadDriver } = require('../index');

const os = require('os');
const platform = os.platform();

async function main () { 
  const UsbBluetoothHciSocket = loadDriver('usb');
  const usbBluetoothHciSocket = new UsbBluetoothHciSocket();
  const usbDevices = await usbBluetoothHciSocket.getDeviceList();
  console.log('usbDevices: ', usbDevices);

  if (platform === 'linux' || platform === 'android') {
    const NativeBluetoothHciSocket = loadDriver('native');
    const nativeBluetoothHciSocket = new NativeBluetoothHciSocket();
    const nativeDevices = await nativeBluetoothHciSocket.getDeviceList();
    console.log('nativeDevices: ', nativeDevices);
  }

  const UartBluetoothHciSocket = loadDriver('uart');
  const uartBluetoothHciSocket = new UartBluetoothHciSocket();
  const uartDevices = await uartBluetoothHciSocket.getDeviceList();
  console.log('uartDevices: ', uartDevices);
}

main();
