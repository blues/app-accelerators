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
  getFleetsByDevice(deviceID: string): Promise<any>;
  getDevicesByFleet(fleetUID: string): Promise<any>;
  getFleetEnvVars(fleetUID: string): Promise<any>;
  doBulkImport(): Promise<BulkImport>;
}
