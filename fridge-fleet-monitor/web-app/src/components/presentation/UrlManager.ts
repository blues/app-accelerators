import { NotificationID } from "../../services/NotificationsStore";

export interface UrlManager {
  gatewayNameUpdate(gatewayUID: string): string;
  nodeNameUpdate(gatewayUID: string, nodeId: string): string;
  notehubProject(notehubUrl: string, projectUID: string): string;

  gatewayDetails(gatewayUID: string): string;
  nodeDetails(gatewayUID: string, nodeId: string): string;
  nodeSettings(gatewayUID: string, nodeId: string): string;

  notifications(...notificationIDs: NotificationID[]): string;
  presentNotifications(...notificationIDs: NotificationID[]): string;
}
