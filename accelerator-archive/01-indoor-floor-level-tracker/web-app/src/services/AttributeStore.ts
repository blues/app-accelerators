import { TrackerConfig } from "./AppModel";
import { DeviceID, FleetID } from "./DomainModel";

export interface AttributeStore {
  updateDeviceName: (deviceUID: DeviceID, name: string) => Promise<void>;

  updateTrackerConfig: (
    fleetUID: FleetID,
    trackerConfig: TrackerConfig
  ) => Promise<void>;
}
