import { NotehubLocationAlternatives } from "./NotehubLocation";
import { SessionMetadata } from "./SessionMetadata";

interface NotehubEvent extends SessionMetadata, NotehubLocationAlternatives {
  file: string;
  captured: string;
  received: string;
  event_uid: string;
  note?: string;
  uid: string;
  device_uid?: string;
  body: {
    count?: number;
    sensor?: string;
    humidity?: number;
    pressure?: number;
    temperature?: number;
    voltage?: number;
    total?: number;
    text?: string;
    why?: string;
    net_updated?: number;
    net?: object;
    loc?: string;
    name?: string;
  };
}

export default NotehubEvent;
