import * as DomainModel from "./DomainModel";

export type ProjectID = DomainModel.ProjectID;
export type DeviceID = DomainModel.DeviceID;
export type EventID = DomainModel.EventID;

/**
 * The presentation models here are json-serializable representations of the domain model.
 */

export interface Project extends DomainModel.Project {
  devices: Device[] | null; // null when devices are not required
}

/**
 * Dates are in UTC time, expressed as seconds since the epoch.
 */
export type AppDate = number;

export type Device = DomainModel.Device;

export type Event = DomainModel.Event;

export type Fleets = DomainModel.Fleets;

export type DeviceEnvVars = DomainModel.DeviceEnvVars;

export type FleetEnvVars = DomainModel.FleetEnvVars;

export interface ProjectDataSnapshot {
  when: AppDate;
  project: Project;
}

export interface ValveMonitorDevice {
  deviceID: string;
  name: string;
  lastActivity: string;
  valveState: string;
  flowRate: number;
  // todo these are for later table functionality
  // lastAlarm?: string | null;
  // valveControl: string | null;  // alarmValveThreshold: number | null;
  // flowRateFrequency: number | null;
}
