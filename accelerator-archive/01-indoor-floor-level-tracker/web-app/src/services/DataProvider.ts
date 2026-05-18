import { DeviceTracker, TrackerConfig } from "./AppModel";

export interface DataProvider {
  getDeviceTrackerData(): Promise<DeviceTracker[]>;
  getTrackerConfig(): Promise<TrackerConfig>;
}
