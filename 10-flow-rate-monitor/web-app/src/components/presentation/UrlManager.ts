import { NotificationID } from "../../services/NotificationsStore";

export interface UrlManager {
  getFlowRateMonitorDeviceData(): string;

  notifications(...notificationIDs: NotificationID[]): string;
  presentNotifications(...notificationIDs: NotificationID[]): string;

  updateFlowRateMonitorDevice(deviceUID: string): string;
  setFleetFlowRateMonitorConfig(): string;

  clearAlarms(): string;
}
