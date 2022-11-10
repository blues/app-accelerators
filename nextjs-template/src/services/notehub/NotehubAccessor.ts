import NotehubDevice from "./models/NotehubDevice";
import NotehubEnvVars from "./models/NotehubEnvVars";
import NotehubEvent from "./models/NotehubEvent";
import NotehubLatestEvents from "./models/NotehubLatestEvents";
import NotehubDeviceConfig from "./models/NotehubDeviceConfig";
import NoteDeviceConfigBody from "./models/NoteDeviceConfigBody";
import NotehubFleetsByProject from "./models/NotehubFleetsByProject";
import NotehubDevicesByFleet from "./models/NotehubDevicesByFleet";

// An interface for accessing Notehub APIs
interface NotehubAccessor {
  getDevices: () => Promise<NotehubDevice[]>;
  getDevice: (hubDeviceUID: string) => Promise<NotehubDevice>;
  getLatestEvents: (hubDeviceUID: string) => Promise<NotehubLatestEvents>;
  getEvents: (startDate?: string) => Promise<NotehubEvent[]>;
  getFleetsByProject: () => Promise<NotehubFleetsByProject>;
  getDevicesByFleet: (fleetUID: string) => Promise<NotehubDevicesByFleet>;
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
