/* eslint-disable @typescript-eslint/naming-convention */
/* eslint-disable class-methods-use-this */
/* eslint-disable @typescript-eslint/no-explicit-any */
import { ValveMonitorConfig, ValveMonitorDevice } from "../AppModel";
import { DataProvider } from "../DataProvider";
import { DeviceEnvVars, Fleets, FleetEnvVars } from "../DomainModel";
import { NotehubLocationAlternatives } from "./models/NotehubLocation";
import { NotehubAccessor } from "./NotehubAccessor";

// N.B.: Noteub defines 'best' location with more nuance than we do here (e.g
// considering staleness). Also this algorthm is copy-pasted in a couple places.
export const getBestLocation = (object: NotehubLocationAlternatives) =>
  object.gps_location || object.triangulated_location || object.tower_location;

export default class NotehubDataProvider implements DataProvider {
  constructor(private readonly notehubAccessor: NotehubAccessor) {}

  async getFleetsByDevice(deviceID: string): Promise<Fleets> {
    const fleetsByDevice = await this.notehubAccessor.getFleetsByDevice(
      deviceID
    );
    return fleetsByDevice;
  }

  async getDeviceEnvVars(deviceID: string): Promise<DeviceEnvVars> {
    const deviceEnvVars = await this.notehubAccessor.getDeviceEnvVars(deviceID);

    const { environment_variables } = deviceEnvVars;
    // attach device ID to env vars for combining data later
    return {
      deviceID,
      environment_variables: {
        ...environment_variables,
        // convert Notehub device monitor frequency from seconds to mins for UI if it exists
        ...(environment_variables.monitor_interval && {
          monitor_interval: `${
            Number(environment_variables?.monitor_interval) / 60
          }`,
        }),
      },
    };
  }

  async getFleetEnvVars(fleetUID: string): Promise<FleetEnvVars> {
    const fleetEnvVars =
      await this.notehubAccessor.getEnvironmentVariablesByFleet(fleetUID);

    const { environment_variables } = fleetEnvVars;
    // attach fleet UID to env vars for combining data later
    return {
      fleetUID,
      environment_variables: {
        ...environment_variables,
        // convert Notehub fleet monitor frequency from seconds to mins for UI if it exists
        ...(environment_variables.monitor_interval && {
          monitor_interval: `${
            Number(environment_variables?.monitor_interval) / 60
          }`,
        }),
      },
    };
  }

  getValveMonitorDeviceData(): Promise<ValveMonitorDevice[]> {
    throw new Error("Method not implemented");
  }

  async getValveMonitorConfig(fleetUID: string): Promise<ValveMonitorConfig> {
    const fleetEnvVars =
      await this.notehubAccessor.getEnvironmentVariablesByFleet(fleetUID);
    const environmentVariables = fleetEnvVars.environment_variables;
    return {
      // convert Notehub fleet monitor frequency from seconds to mins for UI
      monitorFrequency: Number(environmentVariables?.monitor_interval) / 60,
      minFlowThreshold: Number(
        environmentVariables?.flow_rate_alarm_threshold_min
      ),
      maxFlowThreshold: Number(
        environmentVariables?.flow_rate_alarm_threshold_max
      ),
    };
  }
}
