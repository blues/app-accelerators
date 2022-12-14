import { ValveMonitorConfig, ValveMonitorDevice } from "./AppModel";
import { Fleets, FleetEnvVars, DeviceEnvVars } from "./DomainModel";

export interface DataProvider {
  getValveMonitorDeviceData(): Promise<ValveMonitorDevice[]>;
  getFleetsByDevice(deviceID: string): Promise<Fleets>;
  getDeviceEnvVars(deviceID: string): Promise<DeviceEnvVars>;
  getFleetEnvVars(fleetUID: string): Promise<FleetEnvVars>;

  getValveMonitorConfig(fleetUID: string): Promise<ValveMonitorConfig>;
}
