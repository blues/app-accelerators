import { ValveMonitorConfig } from "./AppModel";
import {
  Project,
  Device,
  DeviceID,
  Event,
  Fleets,
  FleetEnvVars,
  DeviceEnvVars,
} from "./DomainModel";

export interface DataProvider {
  getProject(): Promise<Project>;
  getDevices(): Promise<Device[]>;
  getDevice(deviceID: DeviceID): Promise<Device | null>;
  getDeviceEvents(deviceIDs: string[]): Promise<Event[]>;
  getFleetsByProject(): Promise<Fleets>;
  getFleetsByDevice(deviceID: string): Promise<Fleets>;
  getDevicesByFleet(fleetUID: string): Promise<Device[]>;
  getDeviceEnvVars(deviceID: string): Promise<DeviceEnvVars>;
  getFleetEnvVars(fleetUID: string): Promise<FleetEnvVars>;

  getValveMonitorConfig(fleetUID: string): Promise<ValveMonitorConfig>;
}
