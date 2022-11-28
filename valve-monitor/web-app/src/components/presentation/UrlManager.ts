import { NotificationID } from "../../services/NotificationsStore";

export interface UrlManager {
  notehubProject(hubGuiURL: string, hubProjectUID: string): string;

  notifications(...notificationIDs: NotificationID[]): string;
  presentNotifications(...notificationIDs: NotificationID[]): string;

  setFleetValveMonitorConfig(): string;
}
