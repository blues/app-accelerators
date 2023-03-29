import { DeviceTracker, TrackerConfig, AuthToken } from "./AppModel";

export interface DataProvider {
  getAuthToken(): Promise<AuthToken>;
  checkAuthTokenValidity(authTokenString: string): boolean;
  getDeviceTrackerData(authToken: AuthToken): Promise<DeviceTracker[]>;
  getTrackerConfig(authToken: AuthToken): Promise<TrackerConfig>;
}
