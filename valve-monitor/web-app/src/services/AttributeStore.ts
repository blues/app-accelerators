import { DeviceID } from "./DomainModel";

export interface AttributeStore {
  updateDeviceName: (deviceUID: DeviceID, name: string) => Promise<void>;
}
