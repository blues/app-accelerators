import { UrlManager } from "../../components/presentation/UrlManager";
import { NotificationID } from "../../services/NotificationsStore";

export const NextJsUrlManager = {
  bulkDataImport: () => `/admin/bulk-data-import`,
  performBulkDataImportApi: () => `/api/admin/bulk-data-import`,
  gatewayNameUpdate: (gatewayUID: string) => `/api/gateway/${gatewayUID}/name`,
  nodeNameUpdate: (gatewayUID: string, nodeId: string) => `/api/gateway/${gatewayUID}/node/${nodeId}/config`,
  notehubProject: (notehubUrl: string, projectUID: string) => `${notehubUrl}/project/${projectUID}`,

  gatewayDetails: (gatewayUID: string) => `/${gatewayUID}/details`,
  nodeDetails: (gatewayUID: string, nodeId: string) => `/${gatewayUID}/node/${nodeId}/details`,
  nodeSettings: (gatewayUID: string, nodeId: string) => `/${gatewayUID}/node/${nodeId}/details?settings=1`,

  notifications: (...notificationIDs: NotificationID[]) => {
    return NextJsUrlManager.notificationsImpl(false, ...notificationIDs);
  },

  notificationsImpl(present: boolean, ...notificationIDs: string[]) {
    let url = `/api/notifications`;
    let params = present ? "?format=app" : "";
    if (notificationIDs.length) {
      if (!params) {
        params = "?";
      }
      else {
        params += "&";
      }
      // id=abc&id=def
      params += `${notificationIDs.map((id) => `id=${id}`).join("&")}`;
    }
    return url + params;
  },

  presentNotifications: function (...notificationIDs: string[]): string {
    return NextJsUrlManager.notificationsImpl(true, ...notificationIDs);
  }
};

const DEFAULT = { NextJsUrlManager };
export default DEFAULT;
