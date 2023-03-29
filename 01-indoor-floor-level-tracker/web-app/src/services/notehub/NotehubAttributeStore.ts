/* eslint-disable class-methods-use-this */
import * as NotehubJs from "@blues-inc/notehub-js";
import { AttributeStore } from "../AttributeStore";
import { AuthToken, ProjectID, TrackerConfig } from "../AppModel";
import { DeviceID, FleetID } from "../DomainModel";
import { trackerConfigToEnvironmentVariables } from "./NotehubDataProvider";

export default class NotehubAttributeStore implements AttributeStore {
  constructor(
    private readonly projectID: ProjectID,
    private notehubJsClient: any
  ) {}

  async updateDeviceName(
    authToken: AuthToken,
    deviceID: DeviceID,
    name: string
  ) {
    const { bearer_access_token } = this.notehubJsClient.authentications;
    bearer_access_token.accessToken = authToken.access_token;

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

  async updateTrackerConfig(
    authToken: AuthToken,
    fleetUID: FleetID,
    trackerConfig: TrackerConfig
  ) {
    const { bearer_access_token } = this.notehubJsClient.authentications;
    bearer_access_token.accessToken = authToken.access_token;

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
