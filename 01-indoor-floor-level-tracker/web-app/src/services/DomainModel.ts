// The convention is that fields that are entirely optional (and may be absent) are
// marked with ?.  Fields that may have undefined values (e.g. no name set) can also be null.

export interface DeviceID {
  readonly type: "DeviceID";

  /**
   * The natural key for this device, corresponding to the notehub Notecard DeviceUID.
   */
  readonly deviceUID: string;
}

export interface ProjectID {
  readonly type: "ProjectID";
  readonly projectUID: string;
}

export interface FleetID {
  readonly type: "FleetID";
  readonly fleetUID: string;
}

export interface Project {
  readonly id: ProjectID;

  // Attributes
  name: string;
  description: string | null;
  // Links
}

export type Devices = Set<Device>;

export type ProjectWithDevices = Project & {
  devices: Devices;
};

/**
 * Seconds since the epoch
 */
export type DomainDate = number;

export interface Device {
  name: string;
  locationName: string;
  lastSeenAt: string;
  voltage: number;
  readonly id: DeviceID;
}

export interface DateRange {
  from: DomainDate;
  to: DomainDate;
}

export type DurationInMinutes = number;

/**
 * Shows the most recent data
 */
export interface MostRecent {
  duration: DurationInMinutes;
}

export type TimePeriod = DateRange | MostRecent;

export type JSONObject = { [key in string]?: JSONValue };

export type JSONValue =
  | string
  | number
  | boolean
  | JSONObject
  | Array<JSONValue>;
