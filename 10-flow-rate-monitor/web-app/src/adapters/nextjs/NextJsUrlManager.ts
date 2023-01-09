import { NotificationID } from "../../services/NotificationsStore";

export const NextJsUrlManager = {
  getFlowRateMonitorDeviceData: () => `/api/flow-rate-device-monitors`,

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

  updateFlowRateMonitorDevice: (deviceUID: string) =>
    `/api/device/${deviceUID}/flow-rate-monitor`,

  setFleetFlowRateMonitorConfig: () => `/api/fleet/flow-rate-monitor-config`,

  clearAlarms: () => `/api/clear-alarms`,
};

const DEFAULT = { NextJsUrlManager };
export default DEFAULT;
