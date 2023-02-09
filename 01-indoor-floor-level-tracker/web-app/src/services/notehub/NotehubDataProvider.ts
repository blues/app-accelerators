/* eslint-disable @typescript-eslint/no-unsafe-assignment */
/* eslint-disable class-methods-use-this */
import { sub, formatDistanceToNow, parseISO } from "date-fns";
import { uniqBy } from "lodash";
import NotehubJs from "@blues-inc/notehub-js";
import { DeviceTracker, TrackerConfig } from "../AppModel";
import { DataProvider } from "../DataProvider";
import { Device, DeviceID, FleetID, Project, ProjectID } from "../DomainModel";
import NotehubDevice from "./models/NotehubDevice";
import NotehubEnvVars from "./models/NotehubEnvVars";
import { NotehubLocationAlternatives } from "./models/NotehubLocation";
import NotehubRoutedEvent from "./models/NotehubRoutedEvent";
import { NotehubAccessor } from "./NotehubAccessor";
import Config from "../../../Config";

interface HasDeviceId {
  uid: string;
}

// N.B.: Notehub defines 'best' location with more nuance than we do here (e.g
// considering staleness). Also this algorithm is copy-pasted in a couple places.
export const getBestLocation = (object: NotehubLocationAlternatives) =>
  object.gps_location || object.triangulated_location || object.tower_location;

export function notehubDeviceToIndoorTracker(device: NotehubDevice) {
  return {
    uid: device.uid,
    name: device.serial_number,
    lastActivity: device.last_activity,
    ...(getBestLocation(device) && {
      location: getBestLocation(device)?.name,
    }),
    voltage: `${device.voltage}`,
  };
}

export function filterEventsData(events: NotehubRoutedEvent[], file: string) {
  const dataEvent = events.filter((event) => event.file === file).reverse();

  return dataEvent;
}

export function extractRelevantEventBodyData(events: NotehubRoutedEvent[]) {
  const relevantEventInfo = events.map((event) => ({
    ...event.body,
    uid: event.device,
  }));

  return relevantEventInfo;
}

export function mergeObject<CombinedEventObj>(
  A: any,
  B: any
): CombinedEventObj {
  const res: any = {};
  // eslint-disable-next-line @typescript-eslint/no-unsafe-argument, array-callback-return
  Object.keys({ ...A, ...B }).map((key) => {
    // eslint-disable-next-line @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-member-access
    res[key] = A[key] || B[key];
  });
  return res as CombinedEventObj;
}

// merge latest event objects with the same device ID
// these are different readings from the same device
export function reducer<CombinedEventObj extends HasDeviceId>(
  groups: Map<string, CombinedEventObj>,
  event: CombinedEventObj
) {
  // make id the map's key
  const key = event.uid;
  // fetch previous map values associated with that key
  const previous = groups.get(key);
  // combine the previous map event with new map event
  const merged: CombinedEventObj = mergeObject(previous || {}, event);
  // set the key and newly merged object as the value
  groups.set(key, merged);
  return groups;
}

export function formatDeviceTrackerData(deviceTrackerData: DeviceTracker[]) {
  const formattedDeviceTrackerData = deviceTrackerData.map((data) => ({
    ...data,
    lastActivity: formatDistanceToNow(parseISO(data.lastActivity), {
      includeSeconds: true,
    }),
    ...(data.altitude && { altitude: Number(data.altitude).toFixed(1) }),
    voltage: `${Number(data.voltage).toFixed(1)}V`,
    ...(data.pressure && {
      pressure: `${Number(data.pressure).toFixed(1)} hPa`,
    }),
    ...(data.temperature && {
      temp: `${Number(data.temperature).toFixed(1)}C`,
    }),
  }));

  return formattedDeviceTrackerData;
}

export function trackerConfigToEnvironmentVariables(
  trackerConfig: TrackerConfig
) {
  const envVars = {} as NotehubEnvVars;
  if (trackerConfig.baseFloor !== undefined) {
    envVars.baseline_floor = String(trackerConfig.baseFloor);
  }
  if (trackerConfig.floorHeight !== undefined) {
    envVars.floor_height = String(trackerConfig.floorHeight);
  }
  if (trackerConfig.live !== undefined) {
    envVars.live = String(trackerConfig.live);
  }
  if (trackerConfig.noMovementThreshold !== undefined) {
    envVars.no_movement_threshold = String(trackerConfig.noMovementThreshold);
  }
  return envVars;
}

export function environmentVariablesToTrackerConfig(envVars: NotehubEnvVars) {
  return {
    live: envVars.live === "true",
    baseFloor: Number(envVars.baseline_floor) || 1,
    floorHeight: Number(envVars.floor_height) || 4.2672,
    noMovementThreshold: Number(envVars.no_movement_threshold) || 5,
  } as TrackerConfig;
}

export function epochStringMinutesAgo(minutesToConvert: number) {
  const date = new Date();
  const rawEpochDate = sub(date, { minutes: minutesToConvert });
  const formattedEpochDate = Math.round(
    rawEpochDate.getTime() / 1000
  ).toString();
  return formattedEpochDate;
}

export default class NotehubDataProvider implements DataProvider {
  constructor(
    private readonly notehubAccessor: NotehubAccessor,
    private readonly projectID: ProjectID,
    private readonly fleetID: FleetID
  ) {}

  async getDevicesByFleet(): Promise<NotehubDevice[]> {
    const devicesByFleet = await this.notehubAccessor.getDevicesByFleet();
    return devicesByFleet;
  }

  async getEvents(minutesAgo: number): Promise<NotehubRoutedEvent[]> {
    const epochDateMinutesAgo = epochStringMinutesAgo(minutesAgo);
    const events = await this.notehubAccessor.getEvents(epochDateMinutesAgo);
    return events;
  }

  async getDeviceTrackerData(): Promise<DeviceTracker[]> {
    const trackerDevices: DeviceTracker[] = [];
    let formattedDeviceTrackerData: DeviceTracker[] = [];

    // Notehub Open Generator API
    const defaultClient = NotehubJs.ApiClient.instance;
    // Configure API key authorization: api_key
    const { api_key } = defaultClient.authentications;
    api_key.apiKey = Config.hubAuthToken;
    // Uncomment the following line to set a prefix for the API key, e.g. "Token" (defaults to null)
    // api_key.apiKeyPrefix = 'Token';

    const apiInstance = new NotehubJs.ProjectApi();
    const projectUID = Config.hubProjectUID; // String |
    const fleetUID = Config.hubFleetUID;

    const notehubJSDevicesByFleet = await apiInstance.getProjectFleetDevices(
      projectUID,
      fleetUID
    );

    // (data) => {
    console.log(`Devices by Fleet API called successfully. Returned data:`);
    console.log(notehubJSDevicesByFleet.devices);
    // },
    // (error) => {
    //   console.error(error);
    // }
    // );

    // get all the devices by fleet ID
    // const devicesByFleet = await this.getDevicesByFleet();
    // console.log("axios returned data--------------", devicesByFleet);

    // devicesByFleet.forEach((device) => {
    notehubJSDevicesByFleet.devices.forEach((device) => {
      trackerDevices.push(notehubDeviceToIndoorTracker(device));
    });
    const unixTimestampMinutesAgo = epochStringMinutesAgo(6);

    const eventOpts = { startDate: unixTimestampMinutesAgo };

    // todo in future handle for if 'has_more: true'
    const notehubJSEvents = await apiInstance.getProjectEvents(
      projectUID,
      eventOpts
    );
    console.log(`Events API called successfully. Returned data:`);
    console.log(notehubJSEvents.events);

    // fetch events for the last X minutes from Notehub
    // const MINUTES_OF_NOTEHUB_DATA_TO_FETCH = 6;
    // const rawEvents = await this.getEvents(MINUTES_OF_NOTEHUB_DATA_TO_FETCH);
    // console.log("Raw events -------- ", rawEvents);

    // filter down to only data.qo events and reverse the order to get the latest event first
    const filteredEvents = filterEventsData(notehubJSEvents.events, "floor.qo");

    // get unique events by device ID
    const uniqueEvents = uniqBy(filteredEvents, "device");

    // pull out relevant device data from unique events
    const mappedEvents: object[] = extractRelevantEventBodyData(uniqueEvents);

    // concat the device info from fleet with latest device info
    const combinedEventsDevices = [...trackerDevices, ...mappedEvents];

    // combine events with matching device IDs with helper functions defined above
    const reducedEventsIterator = combinedEventsDevices
      .reduce(reducer, new Map())
      .values();

    // transform the Map iterator obj into plain array
    const deviceTrackerData: any[] = Array.from(reducedEventsIterator);

    // format the data to round the numbers to 2 decimal places
    formattedDeviceTrackerData = formatDeviceTrackerData(deviceTrackerData);

    const alarmEvents = filterEventsData(notehubJSEvents.events, "alarm.qo");
    const uniqueAlarmEvents = uniqBy(alarmEvents, "device");
    this.appendAlarmData(uniqueAlarmEvents, formattedDeviceTrackerData);

    return formattedDeviceTrackerData;
  }

  private appendAlarmData(
    alarmEvents: NotehubRoutedEvent[],
    trackers: DeviceTracker[]
  ) {
    alarmEvents.forEach((event) => {
      trackers
        .filter((tracker) => event.device === tracker.uid)
        .forEach((tracker) => {
          // eslint-disable-next-line no-param-reassign
          tracker.lastAlarm = `${event.when}`;
        });
    });
  }

  async getTrackerConfig(): Promise<TrackerConfig> {
    const envVarResponse =
      await this.notehubAccessor.getEnvironmentVariablesByFleet(
        this.fleetID.fleetUID
      );
    const envVars = envVarResponse.environment_variables;
    return environmentVariablesToTrackerConfig(envVars);
  }
}
