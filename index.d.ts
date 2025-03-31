// SPDX-Licence-Indentifier: MIT
// By: Yusuf Can INCE <ycanince@gmail.com>

/// <reference types="node" />

interface Device {
    /** Device ID (UART: port, USB: devId, Native: devId) */
    devId: number | string | null;
    /** Device is up */
    devUp: boolean | null;
    /** USB-IF vendor ID. */
    idVendor: number | null;
    /** USB-IF product ID. */
    idProduct: number | null;
    /** Integer USB device number */
    busNumber: number | null;
    /** Integer USB device address */
    deviceAddress: number | null;
}

interface BindParams {
    usb: {
        vid: number;
        pid: number;
        bus?: number;
        address?: number;
    },
    uart: {
        baudRate: number;
        retryConnection?: number;
    }
}

declare class BluetoothHciSocket extends NodeJS.EventEmitter {
    getDeviceList(): Promise<Device[]>;
    isDevUp(): boolean;

    start(): void;
    stop(): void;
    reset(): void;

    bindRaw(devId: number | string, params?: BindParams): number;
    bindUser(devId: number | string, params?: BindParams): number;
    bindControl(): number;

    setFilter(filter: Buffer): void;
    write(data: Buffer): void;

    on(event: "data", cb: (data: Buffer) => void): this;
    on(event: "error", cb: (error: NodeJS.ErrnoException) => void): this;
}

type DriverType = 'uart' | 'usb' | 'native' | 'unsupported';

declare const bluetooth: {
    loadDriver: (driverType: DriverType) => BluetoothHciSocket;
    default: BluetoothHciSocket;
};

export = bluetooth;
