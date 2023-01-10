import { FlowRateMonitorConfig, FlowRateMonitorDevice } from "./AppModel";
import { Fleets, FleetEnvVars, DeviceEnvVars } from "./DomainModel";

export interface DataProvider {
  getFlowRateMonitorDeviceData(): Promise<FlowRateMonitorDevice[]>;
  getFleetsByDevice(deviceID: string): Promise<Fleets>;
  getDeviceEnvVars(deviceID: string): Promise<DeviceEnvVars>;
  getFleetEnvVars(fleetUID: string): Promise<FleetEnvVars>;

  getFlowRateMonitorConfig(fleetUID: string): Promise<FlowRateMonitorConfig>;
}
