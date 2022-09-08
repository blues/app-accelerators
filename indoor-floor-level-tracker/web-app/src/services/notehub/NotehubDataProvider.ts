/* eslint-disable class-methods-use-this */
/* eslint-disable @typescript-eslint/no-explicit-any */
import { ClientDevice, ClientTracker } from "../ClientModel";
import { DataProvider } from "../DataProvider";
import { Device, DeviceID, Project, ProjectID } from "../DomainModel";
import NotehubDevice from "./models/NotehubDevice";
import NotehubLatestEvents from "./models/NotehubLatestEvents";
import { NotehubLocationAlternatives } from "./models/NotehubLocation";
import { NotehubAccessor } from "./NotehubAccessor";

// N.B.: Noteub defines 'best' location with more nuance than we do here (e.g
// considering staleness). Also this algorthm is copy-pasted in a couple places.
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
  };
}

export function filterLatestEventsData(
  latestDeviceEvents: NotehubLatestEvents
) {
  const dataEvent = latestDeviceEvents.latest_events.filter(
    (event) => event.file === "data.qo"
  );
  return {
    ...dataEvent[0].body,
  };
}

export default class NotehubDataProvider implements DataProvider {
  constructor(
    private readonly notehubAccessor: NotehubAccessor,
    private readonly projectID: ProjectID
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

  async getDevicesByFleet(): Promise<ClientDevice[]> {
    const trackerDevices: ClientDevice[] = [];
    const rawDevices = await this.notehubAccessor.getDevicesByFleet();
    rawDevices.forEach((device) => {
      trackerDevices.push(notehubDeviceToIndoorTracker(device));
    });
    return trackerDevices;
  }

  async getLatestDeviceEvents(deviceID: string): Promise<ClientTracker[]> {
    const trackerLatestEvents: ClientTracker[] = [];
    const rawEvents = await this.notehubAccessor.getLatestEvents(deviceID);
    const filteredEvents = filterLatestEventsData(rawEvents);
    trackerLatestEvents.push({ uid: deviceID, ...filteredEvents });
    return trackerLatestEvents;
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
