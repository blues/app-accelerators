import { FlowRateMonitorConfig } from "../AppModel";
import { AttributeStore } from "../AttributeStore";
import { DeviceID, FleetID } from "../DomainModel";

// would be nice to generify this and implement as a generic proxy
export default class CompositeAttributeStore implements AttributeStore {
  constructor(private stores: AttributeStore[]) {}

  // Retrieves a promise that is resolved when all delegates have completed
  private apply<T>(fn: (store: AttributeStore) => Promise<T>): Promise<T> {
    const all = this.stores.map((store) => fn(store));
    return Promise.all(all).then();
  }

  updateDeviceName(deviceID: DeviceID, name: string): Promise<void> {
    return this.apply((store) => store.updateDeviceName(deviceID, name));
  }

  updateDeviceFlowRateMonitorConfig(
    deviceUID: string,
    flowRateMonitorConfig: FlowRateMonitorConfig
  ) {
    return this.apply((store) =>
      store.updateDeviceFlowRateMonitorConfig(deviceUID, flowRateMonitorConfig)
    );
  }

  updateFlowRateMonitorConfig(
    fleetUID: FleetID,
    flowRateMonitorConfig: FlowRateMonitorConfig
  ) {
    return this.apply((store) =>
      store.updateFlowRateMonitorConfig(fleetUID, flowRateMonitorConfig)
    );
  }
}
