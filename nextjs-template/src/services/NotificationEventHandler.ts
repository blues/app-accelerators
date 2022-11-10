import { randomUUID } from "crypto";
import { _health } from "./notehub/AppEvents";
import { NotificationsStore, Notification } from "./NotificationsStore";
import { AppEvent, AppEventHandler } from "./AppEvent";

/**
 * Pushes notifications to the notification store in response to certain events.
 */

type EventHandler = (event: AppEvent) => Promise<void>;

export class NotificationEventHandler implements AppEventHandler {
  nameHandlers: Map<string, EventHandler> = new Map();

  constructor(private readonly notificationStore: NotificationsStore) {
    this.nameHandlers.set(_health.qo, (e) => this.healthEventHandler(e));
  }

  async healthEventHandler(event: AppEvent): Promise<void> {}

  // todo create a predicate alarm event like isSparrowNodeProvisionedEvent
  // send alarm.qos into the notification table
  // device id required, etc.
  // alarms should go away once acknowledged, events should always be there

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
