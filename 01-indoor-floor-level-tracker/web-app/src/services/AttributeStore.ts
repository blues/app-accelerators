import { AuthToken, TrackerConfig } from "./AppModel";
import { DeviceID, FleetID } from "./DomainModel";

export interface AttributeStore {
  updateDeviceName: (
    authToken: AuthToken,
    deviceUID: DeviceID,
    name: string
  ) => Promise<void>;

  updateTrackerConfig: (
    authToken: AuthToken,
    fleetUID: FleetID,
    trackerConfig: TrackerConfig
  ) => Promise<void>;
}
