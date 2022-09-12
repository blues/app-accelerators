import { randomUUID } from "crypto";
import { _health } from "./notehub/AppEvents";
import { NotificationsStore, Notification } from "./NotificationsStore";
import {
  AppEvent,
  AppEventHandler,
} from "./AppEvent";

/**
 * Pushes notifications to the notification store in response to certain events.
 */

type EventHandler = (event: AppEvent) => Promise<void>;

export class NotificationEventHandler implements AppEventHandler {
  nameHandlers: Map<string, EventHandler> = new Map();
  constructor(private readonly notificationStore: NotificationsStore) {
    this.nameHandlers.set(_health.qo, (e) => this.healthEventHandler(e));
  }

  async healthEventHandler(event: AppEvent): Promise<void> {

  }

  async handleEvent(
    event: AppEvent,
    isHistorical?: boolean | undefined
  ): Promise<void> {
    if (isHistorical) return;
    const handler = this.nameHandlers.get(event.eventName);
    if (handler) {
      await handler(event);
    }
  }
}
