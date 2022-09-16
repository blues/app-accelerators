/* eslint-disable @typescript-eslint/no-unsafe-assignment */
/* eslint-disable class-methods-use-this */
import { sub } from "date-fns";
import { uniqBy } from "lodash";
import {
  ClientDevice,
  ClientTracker,
  DeviceTracker,
  TrackerConfig,
} from "../ClientModel";
import { DataProvider } from "../DataProvider";
import { Device, DeviceID, FleetID, Project, ProjectID } from "../DomainModel";
import NotehubDevice from "./models/NotehubDevice";
import NotehubEnvVars from "./models/NotehubEnvVars";
import { NotehubLocationAlternatives } from "./models/NotehubLocation";
import { NotehubAccessor } from "./NotehubAccessor";

interface HasDeviceId {
  uid: string;
}

// N.B.: Noteub defines 'best' location with more nuance than we do here (e.g
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
    voltage: device.voltage,
  };
}

export function filterLatestEventsData(latestDeviceEvents: any) {
  const dataEvent = latestDeviceEvents.latest_events.filter(
    (event: { file: string }) => event.file === "data.qo"
  );
  // add device uid for later identification of which events belong to which device
  return {
    uid: latestDeviceEvents.uid,
    ...dataEvent[0].body,
  };
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

export function convertMinutesToEpochTime(minutesToConvert: number) {
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

  async getProject(): Promise<Project> {
    const project: Project = {
      id: this.projectID,
      name: "fixme",
      description: "fixme",
    };
    return project;
  }

  async getDevices(): Promise<Device[]> {
    throw new Error("Method not implemented.");
  }

  async getDevice(deviceID: DeviceID): Promise<Device | null> {
    throw new Error("Method not implemented.");
  }

  async getDeviceTrackerData(): Promise<DeviceTracker[]> {
    const trackerDevices: ClientDevice[] = [];
    let formattedDeviceTrackerData: DeviceTracker[] = [];

    // get all the devices by fleet ID
    const rawDevices = await this.notehubAccessor.getDevicesByFleet();
    rawDevices.forEach((device) => {
      trackerDevices.push(notehubDeviceToIndoorTracker(device));
    });

    // fetch events for the last 6 minutes from Notehub
    const startDate = convertMinutesToEpochTime(6);
    const rawEvents = await this.notehubAccessor.getEvents(startDate);

    // filter down to only data.qo events and reverse the order to get the latest event first
    const filteredEvents = rawEvents
      .filter((event) => event.file === "data.qo")
      .reverse();

    // get unique events by device ID
    const uniqueEvents = uniqBy(filteredEvents, "device_uid");

    // pull out relevant device data from unique events
    const mappedEvents: ClientTracker[] = uniqueEvents.map((event) => ({
      ...event.body,
      uid: event.device_uid,
    }));

    // concat the device info from fleet with latest device info
    const combinedEventsDevices = [...trackerDevices, ...mappedEvents];

    // combine events with matching device IDs with helper functions defined above
    const reducedEventsIterator = combinedEventsDevices
      .reduce(reducer, new Map())
      .values();

    // transform the Map iterator obj into plain array
    const deviceTrackerData: any[] = Array.from(reducedEventsIterator);

    // format the data to round the numbers to 2 decimal places
    // eslint-disable-next-line @typescript-eslint/no-unsafe-return
    formattedDeviceTrackerData = deviceTrackerData.map((item) => ({
      ...item,
      ...(item.altitude && { altitude: Number(item.altitude).toFixed(2) }),
      voltage: `${Number(item.voltage).toFixed(2)}V`,
      ...(item.pressure && {
        pressure: `${Number(item.pressure).toFixed(2)} kPa`,
      }),
      ...(item.temp && { temp: `${Number(item.temp).toFixed(2)}C` }),
    }));

    return formattedDeviceTrackerData;
  }

  async getTrackerConfig(): Promise<TrackerConfig> {
    const envVarResponse =
      await this.notehubAccessor.getEnvironmentVariablesByFleet(
        this.fleetID.fleetUID
      );
    const envVars = envVarResponse.environment_variables;
    return environmentVariablesToTrackerConfig(envVars);
  }

  // eslint-disable-next-line class-methods-use-this
  doBulkImport(): Promise<never> {
    throw new Error("It's not possible to do bulk import of data to Notehub");
  }

  /**
   * We made the interface more general (accepting a projectID) but the implementation has the
   * ID fixed. This is a quick check to be sure the project ID is the one expected.
   * @param projectID
   */
  private checkProjectID(projectID: ProjectID) {
    if (projectID.projectUID !== this.projectID.projectUID) {
      throw new Error("Project ID does not match expected ID");
    }
  }
}
