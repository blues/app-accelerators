import { NotificationID } from "../../services/NotificationsStore";

export const NextJsUrlManager = {
  bulkDataImport: () => `/admin/bulk-data-import`,
  performBulkDataImportApi: () => `/api/admin/bulk-data-import`,
  notehubProject: (notehubUrl: string, projectUID: string) =>
    `${notehubUrl}/project/${projectUID}`,
  deviceNameUpdate: (deviceUID: string) => `/api/device/${deviceUID}/name`,
  getDeviceTrackerData: () => `/api/device-trackers`,
  setFleetTrackerConfig: () => `/api/fleet/tracker-config`,

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
};

const DEFAULT = { NextJsUrlManager };
export default DEFAULT;
