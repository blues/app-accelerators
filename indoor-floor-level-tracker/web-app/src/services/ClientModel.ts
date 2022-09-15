export interface ClientDevice {
  uid: string;
  name: string;
  lastActivity: string;
  location?: string;
}

export interface ClientTracker {
  floor?: number | null;
  uid?: string;
}

export interface TrackerConfig {
  live?: boolean;
  baseFloor?: number;
  floorHeight?: number;
  noMovementThreshold?: number;
}

export interface DeviceTracker {
  device: ClientDevice;
  tracker: ClientTracker;
}
