import { NotificationID } from "../../services/NotificationsStore";

export interface UrlManager {
  notehubProject(hubGuiURL: string, hubProjectUID: string): string;
  getDeviceTrackerData(): string;
  setFleetTrackerConfig(): string;

  bulkDataImport(): string;
  performBulkDataImportApi(): string;

  notifications(...notificationIDs: NotificationID[]): string;
  presentNotifications(...notificationIDs: NotificationID[]): string;
}
