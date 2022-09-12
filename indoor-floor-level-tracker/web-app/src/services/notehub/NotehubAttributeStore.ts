import { AttributeStore } from "../AttributeStore";
import { DeviceID } from "../DomainModel";
import { NotehubAccessor } from "./NotehubAccessor";

export default class NotehubAttributeStore implements AttributeStore {
  constructor(private accessor: NotehubAccessor) {}

  async updateDeviceName(deviceID: DeviceID, name: string) {
    await this.accessor.setEnvironmentVariables(deviceID.deviceUID, { _sn: name });
  }

  async updateDevicePin(_deviceID: DeviceID, _pin: string) {
    return Promise.resolve(null);
  }
}
