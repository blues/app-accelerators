import { ValveMonitorConfig } from "../AppModel";
import { AttributeStore } from "../AttributeStore";
import { DeviceID, FleetID } from "../DomainModel";
import NotehubEnvVars from "./models/NotehubEnvVars";
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
    const envVars = {} as NotehubEnvVars;

    if (valveMonitorConfig.monitorFrequency !== undefined) {
      envVars.monitor_interval = `${valveMonitorConfig.monitorFrequency}`;
    }
    if (valveMonitorConfig.minFlowThreshold !== undefined) {
      envVars.flow_rate_alarm_threshold_min = `${valveMonitorConfig.minFlowThreshold}`;
    }
    if (valveMonitorConfig.maxFlowThreshold !== undefined) {
      envVars.flow_rate_alarm_threshold_max = `${valveMonitorConfig.maxFlowThreshold}`;
    }

    await this.accessor.setEnvironmentVariablesByFleet(
      fleetUID.fleetUID,
      envVars
    );
  }
}
