/* eslint-disable import/prefer-default-export */
import { randomUUID } from "crypto";
import { _health, alarm } from "./notehub/AppEvents";
import { AppEvent, AppEventHandler } from "./AppEvent";
import { NotificationsStore } from "./NotificationsStore";

/**
 * Pushes notifications to the notification store in response to certain events.
 */

type EventHandler = (event: AppEvent) => Promise<void>;

// various notification types
export const ALARM = "alarm";

export class NotificationEventHandler implements AppEventHandler {
  nameHandlers: Map<string, EventHandler> = new Map();

  constructor(private readonly notificationStore: NotificationsStore) {
    this.nameHandlers.set(_health.qo, (e) => this.healthEventHandler(e));
    this.nameHandlers.set(alarm.qo, (e) => this.alarmEventHandler(e));
  }

  async healthEventHandler(event: AppEvent): Promise<void> {}

  async alarmEventHandler(event: AppEvent): Promise<void> {
    if (event.eventName === "alarm.qo") {
      const notification = {
        type: ALARM,
        id: randomUUID(),
        when: event.when,
        content: {
          deviceID: event.deviceUID,
          content: event.eventBody,
        },
      };
      await this.notificationStore.addNotifications(notification);
    }
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
