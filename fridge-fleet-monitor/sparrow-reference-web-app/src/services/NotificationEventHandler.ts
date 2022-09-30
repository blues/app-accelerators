import { randomUUID } from "crypto";
import { _health } from "./notehub/SparrowEvents";
import { NotificationsStore, Notification } from "./NotificationsStore";
import {
  isSparrowNodeProvisionedEvent,
  SparrowEvent,
  SparrowEventHandler,
} from "./SparrowEvent";

/**
 * Pushes notifications to the notification store in response to certain events.
 */

type EventHandler = (event: SparrowEvent) => Promise<void>;

export const SPARROW_NODE_PROVISIONED_NOTIFICATION = "node-provisioned";

export type NodePairedNotification = Notification & {
  content: {
    gatewayID: string;
    nodeID: string;
  };
};

export class NotificationEventHandler implements SparrowEventHandler {
  nameHandlers: Map<string, EventHandler> = new Map();
  constructor(private readonly notificationStore: NotificationsStore) {
    this.nameHandlers.set(_health.qo, (e) => this.healthEventHandler(e));
  }

  async healthEventHandler(event: SparrowEvent): Promise<void> {
    if (isSparrowNodeProvisionedEvent(event)) {
      const notification: NodePairedNotification = {
        type: SPARROW_NODE_PROVISIONED_NOTIFICATION,
        id: randomUUID(),
        when: event.when,
        content: {
          nodeID: event.nodeID,
          gatewayID: event.gatewayUID
        },
      };
      await this.notificationStore.addNotifications(notification);
    }
  }

  async handleEvent(
    event: SparrowEvent,
    isHistorical?: boolean | undefined
  ): Promise<void> {
    if (isHistorical) return;
    const handler = this.nameHandlers.get(event.eventName);
    if (handler) {
      await handler(event);
    }
  }
}
