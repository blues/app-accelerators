import { FlowRateMonitorConfig } from "../AppModel";
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

  async updateDeviceFlowRateMonitorConfig(
    deviceID: string,
    deviceflowRateMonitorConfig: FlowRateMonitorConfig
  ) {
    const envVars = {} as NotehubEnvVars;

    if (deviceflowRateMonitorConfig.monitorFrequency !== undefined) {
      // convert device monitor frequency to seconds for Notehub
      envVars.monitor_interval = `${
        deviceflowRateMonitorConfig.monitorFrequency * 60
      }`;
    }
    if (deviceflowRateMonitorConfig.minFlowThreshold !== undefined) {
      envVars.flow_rate_alarm_threshold_min = `${deviceflowRateMonitorConfig.minFlowThreshold}`;
    }
    if (deviceflowRateMonitorConfig.maxFlowThreshold !== undefined) {
      envVars.flow_rate_alarm_threshold_max = `${deviceflowRateMonitorConfig.maxFlowThreshold}`;
    }

    await this.accessor.setEnvironmentVariablesByDevice(deviceID, envVars);
  }

  async updateFlowRateMonitorConfig(
    fleetUID: FleetID,
    flowRateMonitorConfig: FlowRateMonitorConfig
  ) {
    const envVars = {} as NotehubEnvVars;

    if (flowRateMonitorConfig.monitorFrequency !== undefined) {
      // convert fleet monitor frequency to seconds for Notehub
      envVars.monitor_interval = `${Number(
        flowRateMonitorConfig.monitorFrequency * 60
      )}`;
    }
    if (flowRateMonitorConfig.minFlowThreshold !== undefined) {
      envVars.flow_rate_alarm_threshold_min = `${flowRateMonitorConfig.minFlowThreshold}`;
    }
    if (flowRateMonitorConfig.maxFlowThreshold !== undefined) {
      envVars.flow_rate_alarm_threshold_max = `${flowRateMonitorConfig.maxFlowThreshold}`;
    }

    await this.accessor.setEnvironmentVariablesByFleet(
      fleetUID.fleetUID,
      envVars
    );
  }
}
