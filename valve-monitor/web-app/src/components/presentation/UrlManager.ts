import { NotificationID } from "../../services/NotificationsStore";

export interface UrlManager {
  notehubProject(hubGuiURL: string, hubProjectUID: string): string;
  getValveMonitorDeviceData(): string;
  deviceNameUpdate(deviceUID: string): string;

  notifications(...notificationIDs: NotificationID[]): string;
  presentNotifications(...notificationIDs: NotificationID[]): string;

  setDeviceValveMonitorConfig(deviceUID: string): string;
  setFleetValveMonitorConfig(): string;

  updateValveState(deviceUID: string): string;
}
