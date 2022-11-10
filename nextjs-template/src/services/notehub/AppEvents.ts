import { BasicAppEvent, AppEvent } from "../AppEvent";
import { services } from "../ServiceLocatorServer";
import NotehubEvent from "./models/NotehubEvent";
import { NotehubLocationAlternatives } from "./models/NotehubLocation";
import NotehubRoutedEvent, {
  NotehubRoutedEventLocationFields,
} from "./models/NotehubRoutedEvent";

export const _health = {
  qo: "_health.qo",
  SENSOR_PROVISION: "sensor-provision",
};

function eventError(msg: string, event: NotehubRoutedEvent) {
  return new Error(msg);
}

export function locationAlternativesFromRoutedEvent(
  event: NotehubRoutedEventLocationFields
): NotehubLocationAlternatives {
  const alternatives: NotehubLocationAlternatives = {};
  if (
    event.best_location_when &&
    event.best_location &&
    event.best_country &&
    event.best_timezone &&
    event.best_lat &&
    event.best_lon
  ) {
    alternatives.best_location = {
      when: event.best_location_when,
      name: event.best_location,
      country: event.best_country,
      timezone: event.best_timezone,
      latitude: event.best_lat,
      longitude: event.best_lon,
    };
  }
  if (
    event.tower_when &&
    event.tower_location &&
    event.tower_country &&
    event.tower_timezone &&
    event.tower_lat &&
    event.tower_lon
  ) {
    alternatives.tower_location = {
      when: event.tower_when,
      name: event.tower_location,
      country: event.tower_country,
      timezone: event.tower_timezone,
      latitude: event.tower_lat,
      longitude: event.tower_lon,
    };
  }
  if (
    event.when &&
    event.where_location &&
    event.where_country &&
    event.where_timezone &&
    event.where_lat &&
    event.where_lon
  ) {
    alternatives.gps_location = {
      when: event.where_when || event.when,
      name: event.where_location,
      country: event.where_country,
      timezone: event.where_timezone,
      latitude: event.where_lat,
      longitude: event.where_lon,
    };
  }
  if (
    event.tri_when &&
    event.tri_location &&
    event.tri_country &&
    event.tri_timezone &&
    event.tri_lat &&
    event.tri_lon
  ) {
    alternatives.triangulated_location = {
      when: event.tri_when,
      name: event.tri_location,
      country: event.tri_country,
      timezone: event.tri_timezone,
      latitude: event.tri_lat,
      longitude: event.tri_lon,
    };
  }
  return alternatives;
}

// N.B.: Noteub defines 'best' location with more nuance than we do here (e.g
// considering staleness). Also this algorthm is copy-pasted in a couple places.
export const bestLocation = (object: NotehubLocationAlternatives) =>
  object.best_location ||
  object.gps_location ||
  object.triangulated_location ||
  object.tower_location;

function bodyAugmentedWithMetadata(
  event: NotehubRoutedEvent,
  locations: NotehubLocationAlternatives
) {
  // eslint-disable-next-line prefer-destructuring
  const body: { [key: string]: any } = event.body;
  if (event.file === "_session.qo") {
    body.voltage ??= event.voltage;
    body.temp ??= event.temp;
    body.bars ??= event.bars;
    body.gps_location ??= locations.gps_location;
    body.tower_location ??= locations.tower_location;
    body.triangulated_location ??= locations.triangulated_location;
  }
  return body;
}

export async function appEventFromNotehubRoutedEvent(
  event: NotehubRoutedEvent
): Promise<AppEvent> {
  if (!event.device) {
    throw eventError("device is not defined", event);
  }

  if (!event.app) {
    throw eventError("app id is not defined", event);
  }

  const locations = locationAlternativesFromRoutedEvent(event);
  const location = bestLocation(locations);
  const body = bodyAugmentedWithMetadata(event, locations);
  // todo fetch device fleet info here and whatever else is needed
  const appService = services().getAppService();
  const fleets = await appService.getFleetsByDevice(event.device);
  const fleetUIDs: string[] = fleets.fleets.map((fleet) => fleet.uid);

  return new BasicAppEvent(
    event.app,
    event.device,
    event.event,
    new Date(event.when * 1000),
    event.file,
    location,
    body,
    event.sn,
    fleetUIDs
  );
}

export async function appEventFromNotehubEvent(
  event: NotehubEvent,
  projectUID: string
): Promise<AppEvent> {
  if (!event.device_uid) {
    throw eventError("device uid is not defined", event);
  }

  const location = bestLocation(event);
  const body = bodyAugmentedWithMetadata(event, event);
  const appService = services().getAppService();
  const fleets = await appService.getFleetsByDevice(event.device);
  const fleetUIDs: string[] = fleets.fleets.map((fleet) => fleet.uid);

  return new BasicAppEvent(
    projectUID,
    event.device_uid,
    event.event_uid,
    new Date(event.captured),
    event.file,
    location,
    body,
    fleetUIDs
  );
}

export type { AppEvent };
