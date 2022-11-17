import {
  Project,
  Device,
  DeviceID,
  Event,
  Fleets,
  FleetEnvVars,
  DeviceEnvVars,
} from "./DomainModel";
import { ValveMonitorDevice } from "./AppModel";

export interface DataProvider {
  getProject(): Promise<Project>;
  getDevices(): Promise<Device[]>;
  getDevice(deviceID: DeviceID): Promise<Device | null>;
  getValveMonitorDeviceData(): Promise<ValveMonitorDevice[]>;
  getDeviceEvents(deviceIDs: string[]): Promise<Event[]>;
  getFleetsByProject(): Promise<Fleets>;
  getFleetsByDevice(deviceID: string): Promise<Fleets>;
  getDevicesByFleet(fleetUID: string): Promise<Device[]>;
  getDeviceEnvVars(deviceID: string): Promise<DeviceEnvVars>;
  getFleetEnvVars(fleetUID: string): Promise<FleetEnvVars>;
}
