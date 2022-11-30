import NotehubDevice from "./models/NotehubDevice";
import NotehubEnvVars from "./models/NotehubEnvVars";
import NotehubEvent from "./models/NotehubEvent";
import NotehubLatestEvents from "./models/NotehubLatestEvents";
import NotehubDeviceConfig from "./models/NotehubDeviceConfig";
import NoteDeviceConfigBody from "./models/NoteDeviceConfigBody";
import NotehubFleets from "./models/NotehubFleets";
import NotehubDevicesByFleet from "./models/NotehubDevicesByFleet";

// An interface for accessing Notehub APIs
interface NotehubAccessor {
  getDevices: () => Promise<NotehubDevice[]>;
  getDevice: (hubDeviceUID: string) => Promise<NotehubDevice>;
  getLatestEvents: (hubDeviceUID: string) => Promise<NotehubLatestEvents>;
  getEvents: (startDate?: string) => Promise<NotehubEvent[]>;
  getDevicesByFleet: (fleetUID: string) => Promise<NotehubDevicesByFleet>;
  getFleetsByProject: () => Promise<NotehubFleets>;
  getFleetsByDevice: (hubDeviceUID: string) => Promise<NotehubFleets>;
  getDeviceEnvVars: (hubDeviceUID: string) => Promise<NotehubEnvVars>;
  getFleetEnvVars: (fleetUID: string) => Promise<NotehubEnvVars>;
  getConfig: (
    hubDeviceUID: string,
    note: string
  ) => Promise<NotehubDeviceConfig>;
  setConfig: (
    hubDeviceUID: string,
    note: string,
    body: NoteDeviceConfigBody
  ) => Promise<boolean>;
  setEnvironmentVariables: (
    hubDeviceUID: string,
    envVars: NotehubEnvVars
  ) => Promise<boolean>;
}

export type { NotehubAccessor };
