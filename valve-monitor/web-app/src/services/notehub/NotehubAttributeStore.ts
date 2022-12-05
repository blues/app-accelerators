import { ValveMonitorConfig } from "../AppModel";
import { AttributeStore } from "../AttributeStore";
import { DeviceID, FleetID } from "../DomainModel";
import NotehubEnvVars from "./models/NotehubEnvVars";
import { NotehubAccessor } from "./NotehubAccessor";

export default class NotehubAttributeStore implements AttributeStore {
  constructor(private accessor: NotehubAccessor) {}

  async updateDeviceName(deviceID: DeviceID, name: string) {
    await this.accessor.setEnvironmentVariablesByDevice(deviceID.deviceUID, {
      _sn: name,
    });
  }

  async updateDeviceValveMonitorConfig(
    deviceID: string,
    deviceValveMonitorConfig: ValveMonitorConfig
  ) {
    const envVars = {} as NotehubEnvVars;

    if (deviceValveMonitorConfig.monitorFrequency !== undefined) {
      envVars.monitor_interval = `${deviceValveMonitorConfig.monitorFrequency}`;
    }
    if (deviceValveMonitorConfig.minFlowThreshold !== undefined) {
      envVars.flow_rate_alarm_threshold_min = `${deviceValveMonitorConfig.minFlowThreshold}`;
    }
    if (deviceValveMonitorConfig.maxFlowThreshold !== undefined) {
      envVars.flow_rate_alarm_threshold_max = `${deviceValveMonitorConfig.maxFlowThreshold}`;
    }

    await this.accessor.setEnvironmentVariablesByDevice(deviceID, envVars);
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

  async updateValveState(deviceUID: string, state: string) {
    await this.accessor.addNote(deviceUID, "data.qi", { state });
  }
}
