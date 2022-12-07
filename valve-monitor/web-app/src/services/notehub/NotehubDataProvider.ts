/* eslint-disable @typescript-eslint/naming-convention */
/* eslint-disable class-methods-use-this */
/* eslint-disable @typescript-eslint/no-explicit-any */
import { ValveMonitorConfig, ValveMonitorDevice } from "../AppModel";
import { DataProvider } from "../DataProvider";
import {
  Device,
  DeviceEnvVars,
  DeviceID,
  Event,
  Fleets,
  FleetEnvVars,
  Project,
  ProjectID,
} from "../DomainModel";
import { NotehubLocationAlternatives } from "./models/NotehubLocation";
import { NotehubAccessor } from "./NotehubAccessor";

// N.B.: Noteub defines 'best' location with more nuance than we do here (e.g
// considering staleness). Also this algorthm is copy-pasted in a couple places.
export const getBestLocation = (object: NotehubLocationAlternatives) =>
  object.gps_location || object.triangulated_location || object.tower_location;

export default class NotehubDataProvider implements DataProvider {
  constructor(
    private readonly notehubAccessor: NotehubAccessor,
    private readonly projectID: ProjectID
  ) {}

  getProject(): Promise<Project> {
    throw new Error("Method not implemented.");
  }

  getDevices(): Promise<Device[]> {
    throw new Error("Method not implemented.");
  }

  getDevice(deviceID: DeviceID): Promise<Device | null> {
    throw new Error("Method not implemented.");
  }

  getDeviceEvents(deviceIDs: string[]): Promise<Event[]> {
    throw new Error("Method not implemented.");
  }

  async getFleetsByProject(): Promise<Fleets> {
    const fleetsByProject = await this.notehubAccessor.getFleetsByProject();
    return fleetsByProject;
  }

  async getDevicesByFleet(fleetUID: string): Promise<any> {
    const devicesByFleet = await this.notehubAccessor.getDevicesByFleet(
      fleetUID
    );
    return devicesByFleet;
  }

  async getFleetsByDevice(deviceID: string): Promise<Fleets> {
    const fleetsByDevice = await this.notehubAccessor.getFleetsByDevice(
      deviceID
    );
    return fleetsByDevice;
  }

  async getDeviceEnvVars(deviceID: string): Promise<DeviceEnvVars> {
    const deviceEnvVars = await this.notehubAccessor.getDeviceEnvVars(deviceID);

    const { environment_variables } = deviceEnvVars;
    return {
      deviceID,
      environment_variables: {
        ...environment_variables,
        // convert Notehub device monitor frequency from seconds to mins for UI
        monitor_interval: `${
          Number(environment_variables?.monitor_interval) / 60
        }`,
      },
    };
    // attach device ID to env vars for combining data later
    return { deviceID, ...deviceEnvVars };
  }

  async getFleetEnvVars(fleetUID: string): Promise<FleetEnvVars> {
    const fleetEnvVars =
      await this.notehubAccessor.getEnvironmentVariablesByFleet(fleetUID);
    // attach fleet UID to env vars for combining data later
    return { fleetUID, ...fleetEnvVars };
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

  /**
   * We made the interface more general (accepting a projectID) but the implementation has the
   * ID fixed. This is a quick check to be sure the project ID is the one expected.
   * @param projectID
   */
  private checkProjectID(projectID: ProjectID) {
    if (projectID.projectUID !== this.projectID.projectUID) {
      throw new Error("Project ID does not match expected ID");
    }
  }
}
