import { DeviceTracker, TrackerConfig } from "./AppModel";

export interface BulkImport {
  itemCount: number;
  errorCount: number;
}

export interface DataProvider {
  getDeviceTrackerData(): Promise<DeviceTracker[]>;
  getTrackerConfig(): Promise<TrackerConfig>;
}
