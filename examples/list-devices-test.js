const hci = require('../index');
const os = require('os');
const platform = os.platform();

async function main () { 
  const UsbBluetoothHciSocket = hci.loadDriver('usb');
  const usbBluetoothHciSocket = new UsbBluetoothHciSocket();
  const usbDevices = await usbBluetoothHciSocket.getDeviceList();
  console.log('usbDevices: ', usbDevices);

  if (platform === 'linux' || platform === 'android') {
    const NativeBluetoothHciSocket = hci.loadDriver('native');
    const nativeBluetoothHciSocket = new NativeBluetoothHciSocket();
    const nativeDevices = await nativeBluetoothHciSocket.getDeviceList();
    console.log('nativeDevices: ', nativeDevices);
  }

  const UartBluetoothHciSocket = hci.loadDriver('uart');
  const uartBluetoothHciSocket = new UartBluetoothHciSocket();
  const uartDevices = await uartBluetoothHciSocket.getDeviceList();
  console.log('uartDevices: ', uartDevices);
}

main();
