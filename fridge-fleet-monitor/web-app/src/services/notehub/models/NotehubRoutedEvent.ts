import { SessionMetadata } from "./SessionMetadata";

export interface NotehubRoutedEventLocationFields {
  best_location_type?: string;
  best_location_when?: number;
  best_lat?: number;
  best_lon?: number;
  best_location?: string;
  best_country?: string;
  best_timezone?: string;
  where_olc?: string;
  when?: number; // Odd one out
  where_when?: number; // iff this is undefined use `when`
  where_lat?: number;
  where_lon?: number;
  where_location?: string;
  where_country?: string;
  where_timezone?: string;
  tower_when?: number;
  tower_lat?: number;
  tower_lon?: number;
  tower_country?: string;
  tower_location?: string;
  tower_timezone?: string;
  tower_id?: string;
  tri_when?: number;
  tri_lat?: number;
  tri_lon?: number;
  tri_location?: string;
  tri_country?: string;
  tri_timezone?: string;
  tri_points?: number;
}

interface NotehubRoutedEvent extends SessionMetadata, NotehubRoutedEventLocationFields {
  app?: string; // notehub app/project uid
  project?: {   // keeping this for backwards compatibility
    id: string
  }
  device: string; // device uid
  event: string; // event uid
  file: string;
  when: number;
  note?: string;
  received: number;

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

  sn: string;
}

export default NotehubRoutedEvent;
