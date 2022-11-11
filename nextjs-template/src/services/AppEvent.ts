import NotehubLocation from "./notehub/models/NotehubLocation";
import { _health, alarm, notify } from "./notehub/AppEvents";

export interface AppEvent {
  // replace these IDs with typed IDs?

  /**
   * The projectUID of the event
   */
  readonly projectUID: string;

  /**
   * The simplified name of the event.
   */
  readonly eventName: string;

  readonly eventUID: string;

  readonly deviceUID: string;

  // device can be assigned to no fleet manually
  readonly fleetUIDs: string[] | null;

  /**
   * The format of the event depends upon the event type.
   */
  readonly eventBody: { [key: string]: any };

  /**
   * The time the event was published by the origin device.
   */
  readonly when: Date;

  /**
   * The location of the device that forwarded the event
   */
  readonly location?: NotehubLocation;

  readonly deviceName?: string;
}

export class BasicAppEvent implements AppEvent {
  constructor(
    readonly projectUID: string,
    readonly deviceUID: string,
    readonly eventUID: string,
    readonly when: Date,
    readonly eventName: string,
    readonly location: NotehubLocation | undefined,
    readonly eventBody: { [key: string]: any },
    readonly fleetUIDs: string[] | null,
    readonly deviceName?: string
  ) {}
}

export interface AppEventHandler {
  handleEvent(event: AppEvent, isHistorical?: boolean): Promise<void>;
}

export type DeviceHealthEventMethodBody = {
  readonly eventBody: {
    method: string;
    text: string;
  };
};

export function isDeviceHealthEvent(e: AppEvent): e is AppEvent {
  return e.eventName === _health.qo;
}

export function isDeviceAlarmEvent(e: AppEvent): e is AppEvent {
  return e.eventName === alarm.qo;
}

export function isDeviceNotificationEvent(e: AppEvent): e is AppEvent {
  return e.eventName === notify.qo;
}

export function isAlarmEvent(e: AppEvent) {
  return Boolean(isDeviceAlarmEvent(e));
}

export function isNotificationEvent(e: AppEvent) {
  return Boolean(isDeviceNotificationEvent);
}
