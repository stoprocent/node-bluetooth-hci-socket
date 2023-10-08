# node-bluetooth-hci-socket

[![GitHub forks](
https://img.shields.io/github/forks/abandonware/node-bluetooth-hci-socket.svg?style=social&label=Fork&maxAge=2592000
)](
https://GitHub.com/abandonware/node-bluetooth-hci-socket/network/
)
[![license](
https://img.shields.io/badge/license-MIT-0.svg
)](MIT)
[![NPM](
https://img.shields.io/npm/v/@abandonware/bluetooth-hci-socket.svg
)](
https://www.npmjs.com/package/@abandonware/bluetooth-hci-socket
)
[![Fediverse](
https://img.shields.io/mastodon/follow/279303?domain=https%3A%2F%2Fmastodon.social&style=social#rzr
)](
https://mastodon.social/@rzr/103886495590566533#bluetooth-hci-socket
)
[![IRC Channel](
https://img.shields.io/badge/chat-on%20libera-brightgreen.svg
)](
https://kiwiirc.com/client/irc.libera.chat/#iot
)

Bluetooth HCI socket binding for Node.js

__NOTE:__ Currently only supports __Linux__, __FreeBSD__ and __Windows__.

## Prerequisites

 * [node-gyp requirements](https://github.com/TooTallNate/node-gyp#installation)

__NOTE:__ `node-gyp` is only required if the npm cannot find binary for your OS version otherwise the binaries are prebuilt.

### UART/Serial (Any OS)

The reason to use this configuration is more universal transport that can work across multiple operating systems.
Idea is to use Zephyr HCI over UART firmware and to interface with HCI over UART.

##### How to use this?

1. You will need for example NRF5x module (e.g. nRF52840 PDK or nRF52840 USB)
2. 	If you have `nRF52840 PDK` you can in fact just use compiled HEX [misc/nrf52840-usb-cdc.hex](misc/nrf52840-usb-cdc.hex) (Buad Rate set to `1000000`)
3. If you want to compile it yourself:
	1. You will need to install Zephyr ([https://docs.zephyrproject.org/latest/getting_started/index.html](https://docs.zephyrproject.org/latest/getting_started/index.html))
	2. Compile HCI UART Example - `west build -p auto -b <your-board-name> zephyr/samples/bluetooth/hci_uart`
		- e.g. When using `nRF52840 PDK` call `west build -p auto -b nrf52840dk_nrf52840 zephyr/samples/bluetooth/hci_uart`
4. Flash the firmware to Nordic Board e.g. using `nrfjprog`
		- e.g. `nrfjprog -f NRF52 --program misc/nrf52840-usb-cdc.hex –-chiperase --reset`
5. When you get a nordic board connected to the PC/Mac etc. with UART interface you are good to go. Schematic for the connection [can be found here](misc/connection.png).
6. In order to run any example from the examples folder or your own code you have to provide UART port by defining env variable: `BLUETOOTH_HCI_SOCKET_UART_PORT`. Optionally if you use different Baud Rate you can change it by specifing `BLUETOOTH_HCI_SOCKET_UART_BAUDRATE`. Default value is `1000000`
7. e.g. `BLUETOOTH_HCI_SOCKET_UART_PORT=/dev/tty.usbmodem0006837533091 node examples/peripheral-explorer.js b8:27:eb:83:9b:19`

### Linux

 * Bluetooth 4.0 Adapter

__Note:__ the [node-usb](https://github.com/nonolith/node-usb) dependency might fail install, this is ok, because it is an optional optional dependency. Installing ```libudev-dev``` via your Linux distribution's package manager will resolve the problem.

### Windows

This library needs raw USB access to a Bluetooth 4.0 USB adapter, as it needs to bypass the Windows Bluetooth stack.

A [WinUSB](https://msdn.microsoft.com/en-ca/library/windows/hardware/ff540196(v=vs.85).aspx) driver is required, use [Zadig tool](http://zadig.akeo.ie) to replace the driver for your adapter.

__WARNING:__ This will make the adapter unavailable in Windows Bluetooth settings! To roll back to the original driver go to: ```Device Manager -> Open Device -> Update Driver```
Note: 
- that one should select "Delete the driver software for this device" as per Zadig instructions if the generation of the system restoral point by Zadig fails if one wishes to use restore system restoral point as an option.

#### Compatible Bluetooth 4.0 USB Adapter's

| Name | USB VID | USB PID |
|:---- | :------ | :-------|
| BCM920702 Bluetooth 4.0 | 0x0a5c | 0x21e8 |
| BCM920702 Bluetooth 4.0 | 0x0a5c | 0x21f1 |
| BCM20702A0 Bluetooth 4.0 | 0x19ff | 0x0239 |
| BCM20702A0 Bluetooth 4.0 | 0x0489 | 0xe07a |
| BCM20702A0 Bluetooth 4.0 | 0x413c | 0x8143 |
| CSR8510 A10 | 0x0a12 | 0x0001 |
| Asus BT-400 | 0x0b05 | 0x17cb |
| Intel Wireless Bluetooth 6235 | 0x8087 | 0x07da |
| Intel Wireless Bluetooth 7260 | 0x8087 | 0x07dc |
| Intel Wireless Bluetooth 7265 | 0x8087 | 0x0a2a |
| Intel Wireless Bluetooth 8265 | 0x8087 | 0x0a2b |
| Belkin BCM20702A0 | 0x050D | 0x065A |
| Dell Precision 5530| 0x8087 | 0x0025 |

#### Compatible Bluetooth 4.1 USB Adapter's
| Name | USB VID | USB PID |
|:---- | :------ | :-------|
| BCM2045A0 Bluetooth 4.1 | 0x0a5c | 0x6412 |
| Marvell AVASTAR | 0x1286 | 0x204C |

## Install

```sh
npm install @abandonware/bluetooth-hci-socket
```

## Usage

```javascript
var BluetoothHciSocket = require('@abandonware/bluetooth-hci-socket');
```

### Actions

#### Create

```javascript
var bluetoothHciSocket = new BluetoothHciSocket();
```

#### Set Filter

```javascript
var filter = new Buffer(14);

// ...

bluetoothHciSocket.setFilter(filter);
```

__Note:__ ```setFilter``` is not required if ```bindRaw``` is used.

#### Bind

##### Raw Channel

```javascript
bluetoothHciSocket.bindRaw([deviceId]); // optional deviceId (integer)
```

##### User Channel

```javascript
bluetoothHciSocket.bindUser([deviceId]); // optional deviceId (integer)
```

Requires the device to be in the powered down state (```sudo hciconfig hciX down```).

##### Control Channel

```javascript
bluetoothHciSocket.bindControl();
```

#### Is Device Up

Query the device state.

```
var isDevUp = bluetoothHciSocket.isDevUp(); // returns: true or false
```

__Note:__ must be called after ```bindRaw```.

#### Start/stop

Start or stop event handling:

```javascript
bluetoothHciSocket.start();

// ...

bluetoothHciSocket.stop();
```

__Note:__ must be called after ```bindRaw``` or ```bindControl```.

#### Write

```javascript
var data = new Buffer(/* ... */);

// ...


bluetoothHciSocket.write(data);
```

__Note:__ must be called after ```bindRaw``` or ```bindControl```.

### Events

#### Data

```javascript
bluetoothHciSocket.on('data', function(data) {
  // data is a Buffer

  // ...
});
```

#### Error

```javascript
bluetoothHciSocket.on('error', function(error) {
  // error is a Error

  // ...
});
```

## Examples

See [examples folder](https://github.com/abandonware/node-bluetooth-hci-socket/blob/master/examples) for code examples.

## Platform Notes

### Linux

#### Force Raw USB mode

Unload ```btusb``` kernel module:

```sh
sudo rmmod btusb
```

Set ```BLUETOOTH_HCI_SOCKET_FORCE_USB``` environment variable:

```sh
sudo BLUETOOTH_HCI_SOCKET_FORCE_USB=1 node <file>.js
```

### FreeBSD

Disable automatic loading of the default Bluetooth stack by putting [no-ubt.conf](https://gist.github.com/myfreeweb/44f4f3e791a057bc4f3619a166a03b87) into ```/usr/local/etc/devd/no-ubt.conf``` and restarting devd (```sudo service devd restart```).

Unload ```ng_ubt``` kernel module if already loaded:

```sh
sudo kldunload ng_ubt
```

### OS X

#### Disable CSR USB Driver

```sh
sudo kextunload -b com.apple.iokit.CSRBluetoothHostControllerUSBTransport
```

#### Disable Broadcom USB Driver

```sh
sudo kextunload -b com.apple.iokit.BroadcomBluetoothHostControllerUSBTransport
```

### Windows

#### Force adapter USB VID and PID

Set ```BLUETOOTH_HCI_SOCKET_USB_VID``` and ```BLUETOOTH_HCI_SOCKET_USB_PID``` environment variables.

Example for USB device id: 050d:065a:

```sh
set BLUETOOTH_HCI_SOCKET_USB_VID=0x050d
set BLUETOOTH_HCI_SOCKET_USB_PID=0x065a

node <file>.js
```
