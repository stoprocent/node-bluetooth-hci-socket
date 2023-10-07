var BluetoothHciSocket = require('../index');

var bluetoothHciSocket = new BluetoothHciSocket();

bluetoothHciSocket.on('data', function(data) {
  console.log('data: ' + data.toString('hex'));

  if (data.readUInt8(0) === HCI_EVENT_PKT) {
    if (data.readUInt8(1) === EVT_CMD_COMPLETE) {
      if (data.readUInt16LE(4) === LE_SET_SCAN_PARAMETERS_CMD) {
        if (data.readUInt8(6) === HCI_SUCCESS) {
          console.log('LE Scan Parameters Set');
        }
      } else if (data.readUInt16LE(4) === LE_SET_SCAN_ENABLE_CMD) {
        if (data.readUInt8(6) === HCI_SUCCESS) {
          console.log('LE Scan Enable Set');
        }
      }
    } else if (data.readUInt8(1) === EVT_LE_META_EVENT) {
      if (data.readUInt8(3) === EVT_LE_ADVERTISING_REPORT) { // subevent
        var gapAdvType = data.readUInt8(5);
        var gapAddrType = data.readUInt8(6);
        var gapAddr = data.slice(7, 13);

        var eir = data.slice(14, data.length - 1);
        var rssi = data.readInt8(data.length - 1);

        console.log('LE Advertising Report');
        console.log('\t' + ['ADV_IND', 'ADV_DIRECT_IND', 'ADV_SCAN_IND', 'ADV_NONCONN_IND', 'SCAN_RSP'][gapAdvType]);
        console.log('\t' + ['PUBLIC', 'RANDOM'][gapAddrType]);
        console.log('\t' + gapAddr.toString('hex').match(/.{1,2}/g).reverse().join(':'));
        console.log('\t' + eir.toString('hex'));
        console.log('\t' + rssi);
      }
    }
  }
});

bluetoothHciSocket.on('error', function(error) {
  // TODO: non-BLE adaptor

  if (error.message === 'Operation not permitted') {
    console.log('state = unauthorized');
  } else if (error.message === 'Network is down') {
    console.log('state = powered off');
  } else {
    console.error(error);
  }
});

var HCI_COMMAND_PKT = 0x01;
var HCI_ACLDATA_PKT = 0x02;
var HCI_EVENT_PKT = 0x04;
const OGF_HOST_CTL = 0x03;

var EVT_CMD_COMPLETE = 0x0e;
var EVT_CMD_STATUS = 0x0f;
var EVT_LE_META_EVENT = 0x3e;

var EVT_LE_ADVERTISING_REPORT = 0x02;

var OGF_LE_CTL = 0x08;
var OCF_LE_SET_SCAN_PARAMETERS = 0x000b;
var OCF_LE_SET_SCAN_ENABLE = 0x000c;
const OCF_SET_EVENT_MASK = 0x0001;
const OCF_LE_SET_EVENT_MASK = 0x0001;


var LE_SET_SCAN_PARAMETERS_CMD = OCF_LE_SET_SCAN_PARAMETERS | OGF_LE_CTL << 10;
var LE_SET_SCAN_ENABLE_CMD = OCF_LE_SET_SCAN_ENABLE | OGF_LE_CTL << 10;
const SET_EVENT_MASK_CMD = OCF_SET_EVENT_MASK | (OGF_HOST_CTL << 10);
const LE_SET_EVENT_MASK_CMD = OCF_LE_SET_EVENT_MASK | (OGF_LE_CTL << 10);


var HCI_SUCCESS = 0;

function setEventMask() {
  const cmd = Buffer.alloc(12);
  const eventMask = Buffer.from('fffffbff07f8bf3d', 'hex');

  // header
  cmd.writeUInt8(HCI_COMMAND_PKT, 0);
  cmd.writeUInt16LE(SET_EVENT_MASK_CMD, 1);

  // length
  cmd.writeUInt8(eventMask.length, 3);

  eventMask.copy(cmd, 4);

  bluetoothHciSocket.write(cmd);
};

function setLeEventMask() {
  const cmd = Buffer.alloc(12);
  const leEventMask = Buffer.from('1fff000000000000', 'hex')

  // header
  cmd.writeUInt8(HCI_COMMAND_PKT, 0);
  cmd.writeUInt16LE(LE_SET_EVENT_MASK_CMD, 1);

  // length
  cmd.writeUInt8(leEventMask.length, 3);

  leEventMask.copy(cmd, 4);
  bluetoothHciSocket.write(cmd);
};

function setFilter() {
  var filter = new Buffer(14);
  var typeMask = (1 << HCI_EVENT_PKT);
  var eventMask1 = (1 << EVT_CMD_COMPLETE) | (1 << EVT_CMD_STATUS);
  var eventMask2 = (1 << (EVT_LE_META_EVENT - 32));
  var opcode = 0;

  filter.writeUInt32LE(typeMask, 0);
  filter.writeUInt32LE(eventMask1, 4);
  filter.writeUInt32LE(eventMask2, 8);
  filter.writeUInt16LE(opcode, 12);

  bluetoothHciSocket.setFilter(filter);
}

function setScanParameters() {
  var cmd = new Buffer(11);

  // header
  cmd.writeUInt8(HCI_COMMAND_PKT, 0);
  cmd.writeUInt16LE(LE_SET_SCAN_PARAMETERS_CMD, 1);

  // length
  cmd.writeUInt8(0x07, 3);

  // data
  cmd.writeUInt8(0x01, 4); // type: 0 -> passive, 1 -> active
  cmd.writeUInt16LE(0x0010, 5); // internal, ms * 1.6
  cmd.writeUInt16LE(0x0010, 7); // window, ms * 1.6
  cmd.writeUInt8(0x00, 9); // own address type: 0 -> public, 1 -> random
  cmd.writeUInt8(0x00, 10); // filter: 0 -> all event types

  bluetoothHciSocket.write(cmd);
}

function setScanEnable(enabled, duplicates) {
  var cmd = new Buffer(6);

  // header
  cmd.writeUInt8(HCI_COMMAND_PKT, 0);
  cmd.writeUInt16LE(LE_SET_SCAN_ENABLE_CMD, 1);

  // length
  cmd.writeUInt8(0x02, 3);

  // data
  cmd.writeUInt8(enabled ? 0x01 : 0x00, 4); // enable: 0 -> disabled, 1 -> enabled
  cmd.writeUInt8(duplicates ? 0x01 : 0x00, 5); // duplicates: 0 -> no duplicates, 1 -> duplicates

  bluetoothHciSocket.write(cmd);
}


bluetoothHciSocket.bindRaw();
setFilter();
bluetoothHciSocket.start();

setEventMask();
setLeEventMask();

setScanEnable(false, true);

setScanParameters();
setScanEnable(true, true);
