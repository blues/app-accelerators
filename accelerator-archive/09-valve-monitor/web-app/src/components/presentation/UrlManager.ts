import { NotificationID } from "../../services/NotificationsStore";

export interface UrlManager {
  getValveMonitorDeviceData(): string;

  notifications(...notificationIDs: NotificationID[]): string;
  presentNotifications(...notificationIDs: NotificationID[]): string;

  updateValveMonitorDevice(deviceUID: string): string;
  setFleetValveMonitorConfig(): string;

  clearAlarms(): string;
}
