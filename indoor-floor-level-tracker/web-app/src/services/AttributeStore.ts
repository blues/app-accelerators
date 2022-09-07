import { Device, DeviceID } from "./DomainModel";

export interface AttributeStore {
  updateDeviceName: (deviceUID: DeviceID, name: string) => Promise<void>;

  /**
   * Update the pin of the device identified by the given deviceUID.
   * @returns `null` if the device is not found, or the pin is incorrect, otherwise returns the
   * the deviceID.ß
   */
  updateDevicePin: (
    deviceID: DeviceID,
    pin: string
  ) => Promise<Device | null>;
}
