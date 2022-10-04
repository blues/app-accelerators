import NotehubLocation from "./notehub/models/NotehubLocation";
import { _health } from "./notehub/SparrowEvents";

export interface SparrowEvent {
  // replace these IDs with typed IDs?

  /**
   * The projectUID of the event
   */
  readonly projectUID: string;

  /**
   * Events that relate to a specific node have this value set to the node EUI.
   */
  readonly nodeID?: string;

  /**
   * The device UID of the gateway that forwarded this event.
   */
  readonly gatewayUID: string;

  /**
   * The simplified name of the event.
   */
  readonly eventName: string;

  /**
   * The format of the event depends upon the event type.
   */
  readonly eventBody: unknown;

  /**
   * The time the event was published by the origin device.
   */
  readonly when: Date;

  /**
   * The location of the gateway that forwarded the event
   */
  readonly gatewayLocation?: NotehubLocation;

  /**
   * The gateway name that forwarded the event.
   * todo - wrap up in GatewayRoutedEvent that includes the details provided by the gateway for all events
   */
  readonly gatewayName?: string;
}

export interface SparrowNodeEvent extends SparrowEvent {
  /**
   * Events that relate to a specific node have this value set to the node EUI.
   */
  readonly nodeID: string;
}

export class BasicSparrowEvent implements SparrowEvent {
  constructor(
    readonly projectUID: string,
    readonly gatewayUID: string,
    readonly when: Date,
    readonly eventName: string,
    readonly nodeID: string | undefined,
    readonly gatewayLocation: NotehubLocation | undefined,
    readonly eventBody: unknown,
    readonly gatewayName?: string
  ) {}
}

export interface SparrowEventHandler {
  handleEvent(event: SparrowEvent, isHistorical?: boolean): Promise<void>;
}

export type SparrowHealthMethodBody = {
  readonly eventBody: {
    method: string;
    text: string;
  };
};

export type SparrowNodeProvisionedEvent = SparrowHealthMethodBody &
  SparrowNodeEvent;

export function isSparrowNodeEvent(e: SparrowEvent): e is SparrowNodeEvent {
  return !!e.nodeID;
}

export function isSparrowHealthEvent(e: SparrowEvent): e is SparrowEvent {
  return e.eventName === _health.qo;
}

export function isSparrowHealthMethodBody(
  e: SparrowEvent
): e is SparrowHealthMethodBody & SparrowEvent {
  const body: any = e.eventBody;
  return Boolean(isSparrowHealthEvent(e) && body?.method && body?.text);
}

/**
 *
 * @param e A providioning event is both a node event and a health method event, with the method
 * indicating node pairing.
 * @returns
 */
export function isSparrowNodeProvisionedEvent(
  e: SparrowEvent
): e is SparrowNodeProvisionedEvent {
  const body: any = e.eventBody;
  return Boolean(
    isSparrowNodeEvent(e) &&
      isSparrowHealthMethodBody(e) &&
      body.method === _health.SENSOR_PROVISION
  );
}
