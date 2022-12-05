import { NotificationID } from "../../services/NotificationsStore";

export const NextJsUrlManager = {
  notehubProject: (notehubUrl: string, projectUID: string) =>
    `${notehubUrl}/project/${projectUID}`,

  getValveMonitorDeviceData: () => `/api/valve-device-monitors`,

  notifications: (...notificationIDs: NotificationID[]) =>
    NextJsUrlManager.notificationsImpl(false, ...notificationIDs),

  notificationsImpl(present: boolean, ...notificationIDs: string[]) {
    const url = `/api/notifications`;
    let params = present ? "?format=app" : "";
    if (notificationIDs.length) {
      if (!params) {
        params = "?";
      } else {
        params += "&";
      }
      // id=abc&id=def
      params += `${notificationIDs.map((id) => `id=${id}`).join("&")}`;
    }
    return url + params;
  },

  presentNotifications(...notificationIDs: string[]): string {
    return NextJsUrlManager.notificationsImpl(true, ...notificationIDs);
  },

  setDeviceValveMonitorConfig: (deviceUID: string) =>
    `/api/device/${deviceUID}/valve-monitor-config`,

  setFleetValveMonitorConfig: () => `/api/fleet/valve-monitor-config`,

  updateValveState: (deviceUID: string) =>
    `/api/device/${deviceUID}/valve-state`,
};

const DEFAULT = { NextJsUrlManager };
export default DEFAULT;
