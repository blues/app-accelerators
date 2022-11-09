import { Project, Device, DeviceID, Event } from "./DomainModel";

export interface BulkImport {
  itemCount: number;
  errorCount: number;
}

export interface DataProvider {
  getProject(): Promise<Project>;
  getDevices(): Promise<Device[]>;
  getDevice(deviceID: DeviceID): Promise<Device | null>;
  getDeviceEvents(deviceIDs: string[]): Promise<Event[]>;
  getFleetsByProject(): Promise<any>;
  getDevicesByFleet(fleetUID: string): Promise<any>;
  doBulkImport(): Promise<BulkImport>;
}
