import NotehubEnvVars from "./models/NotehubEnvVars";
import NotehubFleets from "./models/NotehubFleets";
import NotehubEnvVarsResponse from "./models/NotehubEnvVarsResponse";

// An interface for accessing Notehub APIs
interface NotehubAccessor {
  getFleetsByDevice: (hubDeviceUID: string) => Promise<NotehubFleets>;
  getDeviceEnvVars: (hubDeviceUID: string) => Promise<NotehubEnvVars>;
  getEnvironmentVariablesByFleet: (
    fleetUID: string
  ) => Promise<NotehubEnvVarsResponse>;
  setEnvironmentVariablesByFleet: (
    fleetUID: string,
    envVars: NotehubEnvVars
  ) => Promise<boolean>;
  setEnvironmentVariablesByDevice: (
    hubDeviceUID: string,
    envVars: NotehubEnvVars
  ) => Promise<boolean>;
  // todo add this to nextjs template
  addNote: (
    hubDeviceUID: string,
    file: string,
    note: object
  ) => Promise<boolean>;
}

export type { NotehubAccessor };
