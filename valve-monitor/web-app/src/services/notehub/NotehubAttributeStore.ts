import { ValveMonitorConfig } from "../AppModel";
import { AttributeStore } from "../AttributeStore";
import { DeviceID, FleetID } from "../DomainModel";
import { NotehubAccessor } from "./NotehubAccessor";

export default class NotehubAttributeStore implements AttributeStore {
  constructor(private accessor: NotehubAccessor) {}

  async updateDeviceName(deviceID: DeviceID, name: string) {
    await this.accessor.setEnvironmentVariables(deviceID.deviceUID, {
      _sn: name,
    });
  }

  async updateValveMonitorConfig(
    fleetUID: FleetID,
    valveMonitorConfig: ValveMonitorConfig
  ) {
    await this.accessor.setEnvironmentVariablesByFleet(fleetUID.fleetUID, {
      monitor_interval: `${valveMonitorConfig.monitorFrequency}`,
    });
  }
}
