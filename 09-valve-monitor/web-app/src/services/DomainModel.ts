// The convention is that fields that are entirely optional (and may be absent) are
// marked with ?.  Fields that may have undefined values (e.g. no name set) can also be null.

export interface DeviceID {
  readonly type: "DeviceID";

  /**
   * The natural key for this device, corresponding to the notehub Notecard DeviceUID.
   */
  readonly deviceUID: string;
}

export interface FleetID {
  readonly type: "FleetID";
  readonly fleetUID: string;
}

export interface EventID {
  readonly type: "EventID";

  readonly eventUID: string;
}

export interface Event {
  readonly id: EventID;

  // device associated with event
  readonly deviceUID: DeviceID;

  // json value event body
  when: string;
  value: any;
}

export interface DeviceEnvVars {
  readonly deviceID: DeviceID["deviceUID"];
  environment_variables?: {
    [key: string]: any;
  };
}

export interface Fleets {
  fleets: [
    {
      uid: string;
      label?: string;
      created?: string;
    }
  ];
}

export interface FleetEnvVars {
  readonly fleetUID?: string;
  environment_variables?: {
    [key: string]: any;
  };
}

export interface ProjectID {
  readonly type: "ProjectID";
  readonly projectUID: string;
}

export interface Project {
  readonly id: ProjectID;

  // Attributes
  name: string;
  description: string | null;
}

export type Devices = Set<Device>;

export type ProjectWithDevices = Project & {
  devices: Devices;
};

export interface Device {
  name: string;
  locationName: string;
  lastSeenAt: string;
  readonly fleetUIDs: string[];
  readonly id: DeviceID;
}

export type JSONObject = { [key in string]?: JSONValue };

export type JSONValue =
  | string
  | number
  | boolean
  | JSONObject
  | Array<JSONValue>;
