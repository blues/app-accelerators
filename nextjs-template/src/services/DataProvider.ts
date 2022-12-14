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
  getEnvironmentVariablesByDevice(deviceID: string): Promise<DeviceEnvVars>;
  getEnvironmentVariablesByFleet(fleetUID: string): Promise<FleetEnvVars>;
}
