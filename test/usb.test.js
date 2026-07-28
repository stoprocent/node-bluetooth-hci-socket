const assert = require('assert');

const BluetoothHciSocket = require('../lib/usb');

function createRawSocket (resetCompleteFallback) {
  const socket = new BluetoothHciSocket();
  const states = [];

  socket._mode = 'raw';
  socket._resetCompleteFallback = resetCompleteFallback;
  socket.on('state', state => states.push(state));

  return { socket, states };
}

describe('USB reset completion', function () {
  it('enables the fallback only for the ASUS BT500', function () {
    const socket = new BluetoothHciSocket();

    assert.strictEqual(socket._needsResetCompleteFallback({
      deviceDescriptor: { idVendor: 0x0b05, idProduct: 0x190e }
    }), true);
    assert.strictEqual(socket._needsResetCompleteFallback({
      deviceDescriptor: { idVendor: 0x0b05, idProduct: 0x17cb }
    }), false);
  });

  it('accepts the standard reset response for every adapter', function () {
    const { socket, states } = createRawSocket(false);

    socket.onHciEventEndpointData(Buffer.from('0e0401030c00', 'hex'));

    assert.strictEqual(socket.isDevUp(), true);
    assert.deepStrictEqual(states, [true]);
  });

  it('accepts a successful non-standard Command Complete for the ASUS BT500', function () {
    const { socket, states } = createRawSocket(true);

    socket.onHciEventEndpointData(Buffer.from('0e0401011000', 'hex'));

    assert.strictEqual(socket.isDevUp(), true);
    assert.deepStrictEqual(states, [true]);
  });

  it('ignores a non-reset Command Complete for other adapters', function () {
    const { socket, states } = createRawSocket(false);

    socket.onHciEventEndpointData(Buffer.from('0e0401011000', 'hex'));

    assert.strictEqual(socket.isDevUp(), false);
    assert.deepStrictEqual(states, []);
  });

  it('ignores a failed Command Complete for the ASUS BT500', function () {
    const { socket, states } = createRawSocket(true);

    socket.onHciEventEndpointData(Buffer.from('0e0401011001', 'hex'));

    assert.strictEqual(socket.isDevUp(), false);
    assert.deepStrictEqual(states, []);
  });
});
