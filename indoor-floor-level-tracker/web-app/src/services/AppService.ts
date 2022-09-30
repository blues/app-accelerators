import { DataProvider } from "./DataProvider";
import { AttributeStore } from "./AttributeStore";
import { DeviceTracker, TrackerConfig } from "./AppModel";
import { IDBuilder } from "./IDBuilder";
import { FleetID } from "./DomainModel";

// this class / interface combo passes data and functions to the service locator file
interface AppServiceInterface {
  setDeviceName: (deviceUID: string, name: string) => Promise<void>;
  getDeviceTrackerData: () => Promise<DeviceTracker[]>;
  getTrackerConfig: () => Promise<TrackerConfig>;
  setTrackerConfig: (trackerConfig: TrackerConfig) => Promise<void>;
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

  async setDeviceName(deviceUID: string, name: string): Promise<void> {
    return this.attributeStore.updateDeviceName(
      this.idBuilder.buildDeviceID(deviceUID),
      name
    );
  }

  async getDeviceTrackerData() {
    return this.dataProvider.getDeviceTrackerData();
  }

  async getTrackerConfig(): Promise<TrackerConfig> {
    return this.dataProvider.getTrackerConfig();
  }

  async setTrackerConfig(trackerConfig: TrackerConfig) {
    const { fleetUID } = this.fleetID;
    return this.attributeStore.updateTrackerConfig(
      this.idBuilder.buildFleetID(fleetUID),
      trackerConfig
    );
  }
}
