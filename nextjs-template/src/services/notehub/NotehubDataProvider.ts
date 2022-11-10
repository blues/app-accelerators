/* eslint-disable class-methods-use-this */
/* eslint-disable @typescript-eslint/no-explicit-any */
import { DataProvider } from "../DataProvider";
import { Device, DeviceID, Project, ProjectID } from "../DomainModel";
import { NotehubLocationAlternatives } from "./models/NotehubLocation";
import { NotehubAccessor } from "./NotehubAccessor";

// N.B.: Noteub defines 'best' location with more nuance than we do here (e.g
// considering staleness). Also this algorthm is copy-pasted in a couple places.
export const getBestLocation = (object: NotehubLocationAlternatives) =>
  object.gps_location || object.triangulated_location || object.tower_location;

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

  async getFleetsByProject(): Promise<any> {
    const fleetsByProject = await this.notehubAccessor.getFleetsByProject();
    return fleetsByProject;
  }

  async getDevicesByFleet(fleetUID: string): Promise<any> {
    const devicesByFleet = await this.notehubAccessor.getDevicesByFleet(
      fleetUID
    );
    return devicesByFleet;
  }

  async getFleetEnvVars(fleetUID: string): Promise<any> {
    const fleetEnvVars = await this.notehubAccessor.getFleetEnvVars(fleetUID);
    // attach fleet UID to env vars for combining data later
    return { fleetUID: fleetUID[0], ...fleetEnvVars };
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
