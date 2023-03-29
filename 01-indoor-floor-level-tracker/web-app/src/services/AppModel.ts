import * as DomainModel from "./DomainModel";

export type ProjectID = DomainModel.ProjectID;
export type FleetID = DomainModel.FleetID;
export type DeviceID = DomainModel.DeviceID;

export interface AuthToken {
  access_token?: string;
  scope?: string;
  expires_in?: number;
  token_type?: string;
  token_expiration_date?: string;
}

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
  direction?: string | null;
  prevFloor?: string | null;
  lastAlarm?: string | null;
}
