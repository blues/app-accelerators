import { AttributeStore } from "../AttributeStore";
import { TrackerConfig } from "../AppModel";
import { DeviceID, FleetID } from "../DomainModel";
import { NotehubAccessor } from "./NotehubAccessor";
import { trackerConfigToEnvironmentVariables } from "./NotehubDataProvider";

export default class NotehubAttributeStore implements AttributeStore {
  constructor(private accessor: NotehubAccessor) {}

  async updateDeviceName(deviceID: DeviceID, name: string) {
    await this.accessor.setEnvironmentVariables(deviceID.deviceUID, {
      _sn: name,
    });
  }

  async updateTrackerConfig(fleetUID: FleetID, trackerConfig: TrackerConfig) {
    await this.accessor.setEnvironmentVariablesByFleet(
      fleetUID.fleetUID,
      trackerConfigToEnvironmentVariables(trackerConfig)
    );
  }
}
