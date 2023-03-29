import { DeviceTracker, TrackerConfig, AuthToken } from "./AppModel";

export interface DataProvider {
  getAuthToken(): Promise<AuthToken>;
  // todo fix this any
  checkAuthTokenValidity(authToken: any): boolean;
  getDeviceTrackerData(authToken: AuthToken): Promise<DeviceTracker[]>;
  getTrackerConfig(authToken: AuthToken): Promise<TrackerConfig>;
}
