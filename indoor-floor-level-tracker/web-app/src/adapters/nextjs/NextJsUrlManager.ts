export const NextJsUrlManager = {
  deviceNameUpdate: (deviceUID: string) => `/api/device/${deviceUID}/name`,
  getDeviceTrackerData: () => `/api/device-trackers`,
  setFleetTrackerConfig: () => `/api/fleet/tracker-config`,
};

const DEFAULT = { NextJsUrlManager };
export default DEFAULT;
