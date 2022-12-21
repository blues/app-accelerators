import NotehubDevice from "./models/NotehubDevice";
import NotehubEnvVars from "./models/NotehubEnvVars";
import NotehubRoutedEvent from "./models/NotehubRoutedEvent";
import NotehubLatestEvents from "./models/NotehubLatestEvents";
import NotehubSensorConfig from "./models/NotehubNodeConfig";
import NoteSensorConfigBody from "./models/NoteNodeConfigBody";

// An interface for accessing Notehub APIs
interface NotehubAccessor {
  getDevices: () => Promise<NotehubDevice[]>;
  getDevice: (hubDeviceUID: string) => Promise<NotehubDevice>;
  getLatestEvents: (hubDeviceUID: string) => Promise<NotehubLatestEvents>;
  getEvents: (startDate?: string) => Promise<NotehubRoutedEvent[]>;
  getConfig: (
    hubDeviceUID: string,
    nodeId: string
  ) => Promise<NotehubSensorConfig>;
  setConfig: (
    hubDeviceUID: string,
    nodeId: string,
    body: NoteSensorConfigBody
  ) => Promise<boolean>;
  setEnvironmentVariables: (
    hubDeviceUID: string,
    envVars: NotehubEnvVars
  ) => Promise<boolean>;
}

// eslint-disable-next-line import/prefer-default-export
export type { NotehubAccessor };
