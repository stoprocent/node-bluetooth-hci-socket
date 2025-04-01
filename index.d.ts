/// <reference types="node" />
declare module '@stoprocent/bluetooth-hci-socket' {
    
    import { EventEmitter } from 'events';
    
    export interface Device {
        /** Device ID */
        devId: number | null;
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
        /** Device path (UART) */
        path: string | null;
    }

    export interface BindParams {
        usb: {
            vid: number;
            pid: number;
            bus?: number;
            address?: number;
        } | undefined;
        uart: {
            port: string;
            baudRate: number;
            retryConnection?: number;
            flowControl?: boolean;
        } | undefined;
    }

    export class BluetoothHciSocket extends EventEmitter {
        getDeviceList(): Promise<Device[]>;
        isDevUp(): boolean;

        start(): void;
        stop(): void;
        reset(): void;

        bindRaw(devId: number, params?: BindParams): number;
        bindUser(devId: number, params?: BindParams): number;
        bindControl(): number;

        setFilter(filter: Buffer): void;
        write(data: Buffer): void;

        on(event: "data", cb: (data: Buffer) => void): this;
        on(event: "error", cb: (error: NodeJS.ErrnoException) => void): this;
    }

    export type DriverType = 'default' | 'uart' | 'usb' | 'native';

    export function loadDriver(driverType: DriverType): typeof BluetoothHciSocket;
    
    // Define a default export
    const BluetoothHciSocketDefault: typeof BluetoothHciSocket;
    export default BluetoothHciSocketDefault;
}