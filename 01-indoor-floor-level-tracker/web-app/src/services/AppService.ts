import { DataProvider } from "./DataProvider";
import { AttributeStore } from "./AttributeStore";
import { AuthToken, DeviceTracker, TrackerConfig } from "./AppModel";
import { IDBuilder } from "./IDBuilder";
import { FleetID } from "./DomainModel";

// this class / interface combo passes data and functions to the service locator file
interface AppServiceInterface {
  getAuthToken: () => Promise<AuthToken>;
  // todo fix this any
  checkAuthTokenValidity: (authToken: any) => boolean;
  setDeviceName: (
    authToken: AuthToken,
    deviceUID: string,
    name: string
  ) => Promise<void>;
  getDeviceTrackerData: (authToken: AuthToken) => Promise<DeviceTracker[]>;
  getTrackerConfig: (authToken: AuthToken) => Promise<TrackerConfig>;
  setTrackerConfig: (
    authToken: AuthToken,
    trackerConfig: TrackerConfig
  ) => Promise<void>;
}

export type { AppServiceInterface };

export default class AppService implements AppServiceInterface {
  private fleetID: FleetID;

  constructor(
    fleetUID: string,
    private readonly idBuilder: IDBuilder,
    private dataProvider: DataProvider,
    private attributeStore: AttributeStore
  ) {
    this.fleetID = this.idBuilder.buildFleetID(fleetUID);
  }

  async getAuthToken() {
    return this.dataProvider.getAuthToken();
  }

  // todo fix this any
  checkAuthTokenValidity(authToken: any): boolean {
    return this.dataProvider.checkAuthTokenValidity(authToken);
  }

  async setDeviceName(
    authToken: AuthToken,
    deviceUID: string,
    name: string
  ): Promise<void> {
    return this.attributeStore.updateDeviceName(
      authToken,
      this.idBuilder.buildDeviceID(deviceUID),
      name
    );
  }

  async getDeviceTrackerData(authToken: AuthToken) {
    return this.dataProvider.getDeviceTrackerData(authToken);
  }

  async getTrackerConfig(authToken: AuthToken): Promise<TrackerConfig> {
    return this.dataProvider.getTrackerConfig(authToken);
  }

  async setTrackerConfig(authToken: AuthToken, trackerConfig: TrackerConfig) {
    const { fleetUID } = this.fleetID;
    return this.attributeStore.updateTrackerConfig(
      authToken,
      this.idBuilder.buildFleetID(fleetUID),
      trackerConfig
    );
  }
}
