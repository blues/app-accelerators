import NotehubDevice from "./models/NotehubDevice";
import NotehubEnvVars from "./models/NotehubEnvVars";
import NotehubEnvVarsResponse from "./models/NotehubEnvVarsResponse";
import NotehubRoutedEvent from "./models/NotehubRoutedEvent";

// An interface for accessing Notehub APIs
interface NotehubAccessor {
  getEvents: (startDate?: string) => Promise<NotehubRoutedEvent[]>;
  getEnvironmentVariablesByFleet: (
    fleetUID: string
  ) => Promise<NotehubEnvVarsResponse>;
  setEnvironmentVariables: (
    hubDeviceUID: string,
    envVars: NotehubEnvVars
  ) => Promise<boolean>;
  getDevicesByFleet: () => Promise<NotehubDevice[]>;
  setEnvironmentVariablesByFleet: (
    fleetUID: string,
    envVars: NotehubEnvVars
  ) => Promise<boolean>;
}

export type { NotehubAccessor };
