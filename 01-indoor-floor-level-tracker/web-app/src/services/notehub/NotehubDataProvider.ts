/* eslint-disable @typescript-eslint/no-unsafe-assignment */
/* eslint-disable class-methods-use-this */
import { sub, formatDistanceToNow, parseISO, isPast } from "date-fns";
import { uniqBy } from "lodash";
import * as NotehubJs from "@blues-inc/notehub-js";
import { DeviceTracker, TrackerConfig, AuthToken } from "../AppModel";
import { DataProvider } from "../DataProvider";
import { FleetID, ProjectID } from "../DomainModel";
import NotehubDevice from "./models/NotehubDevice";
import NotehubEnvVars from "./models/NotehubEnvVars";
import { NotehubLocationAlternatives } from "./models/NotehubLocation";
import NotehubRoutedEvent from "./models/NotehubRoutedEvent";
import NotehubEnvVarsResponse from "./models/NotehubEnvVarsResponse";
import NotehubAccessToken from "./models/NotehubAccessToken";
import { HTTP_AUTH } from "../../constants/http";

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
    // convert notehub no_move_threshold from seconds to mins for UI
    envVars.no_movement_threshold = String(
      trackerConfig.noMovementThreshold / 60
    );
  }
  return envVars;
}

export function environmentVariablesToTrackerConfig(envVars: NotehubEnvVars) {
  return {
    live: envVars.live === "true",
    baseFloor: Number(envVars.baseline_floor) || 1,
    floorHeight: Number(envVars.floor_height) || 4.2672,
    // convert UI no_move_threshold from minutes to seconds for Notehub
    noMovementThreshold:
      Number(Number(envVars.no_movement_threshold) * 60) || 300,
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
    private readonly projectID: ProjectID,
    private readonly fleetID: FleetID,
    private readonly hubClientId: string,
    private readonly hubClientSecret: string,
    private readonly notehubJsClient: any
  ) {}

  async getAuthToken(): Promise<AuthToken> {
    const authApiInstance = new NotehubJs.AuthorizationApi();

    const opts = {
      grantType: HTTP_AUTH.GRANT_TYPE,
      clientId: this.hubClientId,
      clientSecret: this.hubClientSecret,
    };
    const accessToken: NotehubAccessToken =
      await authApiInstance.generateAuthToken(opts);

    // eslint-disable-next-line @typescript-eslint/naming-convention
    const thirtyMinutesFromNowInSeconds =
      (Math.floor(Date.now() / 1000) + accessToken.expires_in) * 1000;
    return {
      token_expiration_date: `${thirtyMinutesFromNowInSeconds}`,
      ...accessToken,
    };
  }

  checkAuthTokenValidity(authTokenString: string): boolean {
    // no auth token found, generate a new one
    if (authTokenString === undefined) {
      false;
    }

    // extract token expiration date from authTokenString to check if its past or not
    const parsedAuthTokenObj = JSON.parse(authTokenString);
    const normalizedTokenExpiration = parseInt(
      parsedAuthTokenObj.token_expiration_date,
      10
    );
    // auth token expired, generate a new one
    if (isPast(normalizedTokenExpiration)) {
      false;
    }
    // current auth token still valid
    return true;
  }

  async getDeviceTrackerData(authToken: AuthToken): Promise<DeviceTracker[]> {
    const trackerDevices: DeviceTracker[] = [];
    let formattedDeviceTrackerData: DeviceTracker[] = [];

    const { bearer_access_token } = this.notehubJsClient.authentications;
    bearer_access_token.accessToken = authToken.access_token;

    const projectApiInstance = new NotehubJs.ProjectApi();
    const { projectUID } = this.projectID;
    const { fleetUID } = this.fleetID;

    const devicesByFleet = await projectApiInstance.getProjectFleetDevices(
      projectUID,
      fleetUID
    );

    // get all the devices by fleet ID
    devicesByFleet.devices.forEach((device: NotehubDevice) => {
      trackerDevices.push(notehubDeviceToIndoorTracker(device));
    });

    // fetch all events for the last X minutes from Notehub
    const MINUTES_OF_NOTEHUB_DATA_TO_FETCH = 6;
    const unixTimestampMinutesAgo = epochStringMinutesAgo(
      MINUTES_OF_NOTEHUB_DATA_TO_FETCH
    );
    const eventOpts = { startDate: unixTimestampMinutesAgo };

    bearer_access_token.accessToken = authToken.access_token;

    const rawEvents = await projectApiInstance.getProjectEvents(
      projectUID,
      eventOpts
    );

    // filter down to only data.qo events and reverse the order to get the latest event first
    const filteredEvents = filterEventsData(rawEvents.events, "floor.qo");

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
    const deviceTrackerData: DeviceTracker[] = Array.from(
      reducedEventsIterator
    );

    // format the data to round the numbers to 2 decimal places
    formattedDeviceTrackerData = formatDeviceTrackerData(deviceTrackerData);

    const alarmEvents = filterEventsData(rawEvents.events, "alarm.qo");
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

  async getTrackerConfig(authToken: AuthToken): Promise<TrackerConfig> {
    const { bearer_access_token } = this.notehubJsClient.authentications;
    bearer_access_token.accessToken = authToken.access_token;

    const fleetApiInstance = new NotehubJs.FleetApi();
    const { projectUID } = this.projectID;
    const { fleetUID } = this.fleetID;

    const envVarResponse: NotehubEnvVarsResponse =
      await fleetApiInstance.getFleetEnvironmentVariables(projectUID, fleetUID);
    const envVars = envVarResponse.environment_variables;

    return environmentVariablesToTrackerConfig(envVars);
  }
}
