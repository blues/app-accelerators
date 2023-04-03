export interface UrlManager {
  getDeviceTrackerData(): string;
  deviceNameUpdate(deviceUID: string): string;
  setFleetTrackerConfig(): string;
  handleAuthToken(): string;
}
