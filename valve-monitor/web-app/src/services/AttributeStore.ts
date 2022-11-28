import { ValveMonitorConfig } from "./AppModel";
import { DeviceID, FleetID } from "./DomainModel";

export interface AttributeStore {
  updateDeviceName: (deviceUID: DeviceID, name: string) => Promise<void>;
  updateValveMonitorConfig: (
    fleetUID: FleetID,
    valveMonitorConfig: ValveMonitorConfig
  ) => Promise<void>;
}
