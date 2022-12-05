import { ValveMonitorConfig } from "./AppModel";
import { DeviceID, FleetID } from "./DomainModel";

export interface AttributeStore {
  updateDeviceName: (deviceUID: DeviceID, name: string) => Promise<void>;
  updateDeviceValveMonitorConfig: (
    deviceUID: string,
    valveMonitorConfig: ValveMonitorConfig
  ) => Promise<void>;
  updateValveMonitorConfig: (
    fleetUID: FleetID,
    valveMonitorConfig: ValveMonitorConfig
  ) => Promise<void>;
  updateValveState: (deviceUID: string, state: string) => Promise<void>;
}
