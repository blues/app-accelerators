import { DeviceTracker, TrackerConfig } from "./ClientModel";
import { Project, Device, DeviceID } from "./DomainModel";

export interface BulkImport {
  itemCount: number;
  errorCount: number;
}

export interface DataProvider {
  getProject(): Promise<Project>;
  getDevices(): Promise<Device[]>;
  getDevice(deviceID: DeviceID): Promise<Device | null>;
  doBulkImport(): Promise<BulkImport>;
  getDeviceTrackerData(): Promise<DeviceTracker[]>;
  getTrackerConfig(): Promise<TrackerConfig>;
}
