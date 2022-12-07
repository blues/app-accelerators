import { NotificationID } from "../../services/NotificationsStore";

export interface UrlManager {
  notehubProject(hubGuiURL: string, hubProjectUID: string): string;
  getValveMonitorDeviceData(): string;

  notifications(...notificationIDs: NotificationID[]): string;
  presentNotifications(...notificationIDs: NotificationID[]): string;

  updateValveMonitorDevice(deviceUID: string): string;
  setFleetValveMonitorConfig(): string;
}
