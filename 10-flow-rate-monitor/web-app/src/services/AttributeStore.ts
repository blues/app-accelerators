import { FlowRateMonitorConfig } from "./AppModel";
import { DeviceID, FleetID } from "./DomainModel";

export interface AttributeStore {
  updateDeviceName: (deviceUID: DeviceID, name: string) => Promise<void>;
  updateDeviceFlowRateMonitorConfig: (
    deviceUID: string,
    flowRateMonitorConfig: FlowRateMonitorConfig
  ) => Promise<void>;
  updateFlowRateMonitorConfig: (
    fleetUID: FleetID,
    flowRateMonitorConfig: FlowRateMonitorConfig
  ) => Promise<void>;
}
