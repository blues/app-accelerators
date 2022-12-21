// todo - this is quite generic. Could be moved out of the notehub package.

export interface NotehubLocationAlternatives {
  gps_location?: NotehubLocation;
  tower_location?: NotehubLocation;
  triangulated_location?: NotehubLocation;
}
interface NotehubLocation {
  when: number;
  name: string;
  country: string;
  timezone: string;
  latitude: number;
  longitude: number;
}

export default NotehubLocation;
