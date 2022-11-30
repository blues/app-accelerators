/* eslint-disable import/prefer-default-export */
import { _health } from "./notehub/AppEvents";
import { AppEvent, AppEventHandler } from "./AppEvent";
import { NotificationsStore } from "./NotificationsStore";

/**
 * Pushes notifications to the notification store in response to certain events.
 */

type EventHandler = (event: AppEvent) => Promise<void>;

// various notification types go here
export class NotificationEventHandler implements AppEventHandler {
  nameHandlers: Map<string, EventHandler> = new Map();

  constructor(private readonly notificationStore: NotificationsStore) {
    this.nameHandlers.set(_health.qo, (e) => this.healthEventHandler(e));
  }

  async healthEventHandler(event: AppEvent): Promise<void> {}

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
