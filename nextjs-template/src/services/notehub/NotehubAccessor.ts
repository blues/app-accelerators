import NotehubDevice from "./models/NotehubDevice";
import NotehubEnvVars from "./models/NotehubEnvVars";
import NotehubEvent from "./models/NotehubEvent";
import NotehubLatestEvents from "./models/NotehubLatestEvents";
import NotehubDeviceConfig from "./models/NotehubDeviceConfig";
import NoteDeviceConfigBody from "./models/NoteDeviceConfigBody";
import NotehubFleets from "./models/NotehubFleets";
import NotehubDevicesByFleet from "./models/NotehubDevicesByFleet";
import NotehubEnvVarsResponse from './models/NotehubEnvVarsResponse'

// An interface for accessing Notehub APIs
interface NotehubAccessor {
  // devices
  getDevices: () => Promise<NotehubDevice[]>;
  getDevice: (hubDeviceUID: string) => Promise<NotehubDevice>;
  getDevicesByFleet: (fleetUID: string) => Promise<NotehubDevicesByFleet>;

  // device config 
  getConfig: (
    hubDeviceUID: string,
    note: string
  ) => Promise<NotehubDeviceConfig>;
  setConfig: (
    hubDeviceUID: string,
    note: string,
    body: NoteDeviceConfigBody
  ) => Promise<boolean>;

  // events
  getLatestEvents: (hubDeviceUID: string) => Promise<NotehubLatestEvents>;
  getEvents: (startDate?: string) => Promise<NotehubEvent[]>;

  // fleets
  getFleetsByProject: () => Promise<NotehubFleets>;
  getFleetsByDevice: (hubDeviceUID: string) => Promise<NotehubFleets>;

  // env vars by device
  getEnvironmentVariablesByDevice: (hubDeviceUID: string) => Promise<NotehubEnvVars>;
  setEnvironmentVariablesByDevice: (
    hubDeviceUID: string,
    envVars: NotehubEnvVars
  ) => Promise<boolean>;

  // env vars by fleet
  getEnvironmentVariablesByFleet: (fleetUID: string) => Promise<NotehubEnvVarsResponse>;
  setEnvironmentVariablesByFleet: (
    fleetUID: string,
    envVars: NotehubEnvVars
  ) => Promise<boolean>;

  // notes
  addNote: (
    hubDeviceUID: string,
    file: string,
    note: object
  ) => Promise<boolean>;
}

export type { NotehubAccessor };
