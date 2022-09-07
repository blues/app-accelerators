import { NotificationID } from "../../services/NotificationsStore";

export interface UrlManager {
  notehubProject(hubGuiURL: string, hubProjectUID: string): string;
  
  bulkDataImport(): string;
  performBulkDataImportApi(): string;

  notifications(...notificationIDs: NotificationID[]): string;
  presentNotifications(...notificationIDs: NotificationID[]): string;
}
