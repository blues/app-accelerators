export interface TrackerConfig {
  live?: boolean;
  baseFloor?: number;
  floorHeight?: number;
  noMovementThreshold?: number;
}

export interface DeviceTracker {
  uid: string;
  name: string;
  lastActivity: string;
  location?: string;
  voltage: string;
  floor?: string | null;
  pressure?: string | null;
  temperature?: string | null;
  altitude?: string | null;
}
