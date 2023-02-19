/* eslint-disable class-methods-use-this */
import * as NotehubJs from "@blues-inc/notehub-js";
import { AttributeStore } from "../AttributeStore";
import { ProjectID, TrackerConfig } from "../AppModel";
import { DeviceID, FleetID } from "../DomainModel";
import { trackerConfigToEnvironmentVariables } from "./NotehubDataProvider";

export default class NotehubAttributeStore implements AttributeStore {
  constructor(
    private readonly projectID: ProjectID,
    private readonly hubAuthToken: string,
    private notehubJsClient: any
  ) {}

  async updateDeviceName(deviceID: DeviceID, name: string) {
    const { notehubJsClient } = this;
    const { api_key } = notehubJsClient.authentications;
    api_key.apiKey = this.hubAuthToken;

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
    const { notehubJsClient } = this;
    const { api_key } = notehubJsClient.authentications;
    api_key.apiKey = this.hubAuthToken;

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
