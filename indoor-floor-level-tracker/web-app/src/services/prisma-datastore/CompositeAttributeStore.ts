import { AttributeStore } from "../AttributeStore";
import { TrackerConfig } from "../ClientModel";
import { Device, DeviceID, FleetID } from "../DomainModel";

// would be nice to generify this and implement as a generic proxy
export default class CompositeAttributeStore implements AttributeStore {
  constructor(private stores: AttributeStore[]) {}

  updateTrackerConfig: (
    fleetUID: FleetID,
    trackerConfig: TrackerConfig
  ) => Promise<void> = () => {
    throw new Error("not implemented");
  };

  // Retrieves a promise that is resolved when all delegates have completed
  private apply<T>(fn: (store: AttributeStore) => Promise<T>): Promise<T> {
    const all = this.stores.map((store) => fn(store));
    return Promise.all(all).then();
  }

  updateDeviceName(deviceID: DeviceID, name: string): Promise<void> {
    return this.apply((store) => store.updateDeviceName(deviceID, name));
  }

  updateDevicePin(deviceID: DeviceID, pin: string): Promise<Device | null> {
    return this.apply((store) => store.updateDevicePin(deviceID, pin));
  }
}
