/* eslint-disable class-methods-use-this */
import * as NotehubJs from "@blues-inc/notehub-js";
import { AttributeStore } from "../AttributeStore";
import { ProjectID, TrackerConfig } from "../AppModel";
import { DeviceID, FleetID } from "../DomainModel";
import {
  trackerConfigToEnvironmentVariables,
  checkAuthTokenValidity,
} from "./NotehubDataProvider";

export default class NotehubAttributeStore implements AttributeStore {
  constructor(
    private readonly projectID: ProjectID,
    private readonly hubClientId: string,
    private readonly hubClientSecret: string,
    private notehubJsClient: any
  ) {}

  async updateDeviceName(deviceID: DeviceID, name: string) {
    await checkAuthTokenValidity(
      this.notehubJsClient,
      this.hubClientId,
      this.hubClientSecret
    );

    const envVarApiInstance = new NotehubJs.EnvironmentVariablesApi();
    const { projectUID } = this.projectID;
    const { deviceUID } = deviceID;
    const environmentVariables = new NotehubJs.EnvironmentVariables({
      _sn: name,
    });

    await envVarApiInstance.putDeviceEnvironmentVariables(
      projectUID,
      deviceUID,
      environmentVariables
    );
  }

  async updateTrackerConfig(fleetUID: FleetID, trackerConfig: TrackerConfig) {
    await checkAuthTokenValidity(
      this.notehubJsClient,
      this.hubClientId,
      this.hubClientSecret
    );

    const envVarApiInstance = new NotehubJs.EnvironmentVariablesApi();
    const { projectUID } = this.projectID;
    const formattedTrackerConfig =
      trackerConfigToEnvironmentVariables(trackerConfig);
    const environmentVariables = new NotehubJs.EnvironmentVariables(
      formattedTrackerConfig
    );

    await envVarApiInstance.putFleetEnvironmentVariables(
      projectUID,
      fleetUID.fleetUID,
      environmentVariables
    );
  }
}
