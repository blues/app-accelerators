/* eslint-disable class-methods-use-this */
/* eslint-disable no-restricted-syntax */
/* eslint-disable no-await-in-loop */
/* eslint-disable import/prefer-default-export */
import Prisma, { PrismaClient } from "@prisma/client";
import { DataProvider } from "../DataProvider";
import { ProjectID, DeviceID, Device, Project, Event } from "../DomainModel";

import IDBuilder from "../IDBuilder";

/**
 * Implements the DataProvider service using Prisma ORM.
 */
export class PrismaDataProvider implements DataProvider {
  constructor(private prisma: PrismaClient, private projectID: ProjectID) {}

  async getProject(): Promise<Project> {
    const project = await this.currentProject();
    return {
      id: this.projectID,
      name: project.name,
      description: null,
    };
  }

  private currentProjectID(): ProjectID {
    return this.projectID;
  }

  private async currentProject(): Promise<Prisma.Project> {
    // this is intentionally oversimplified - later will need to consider the current logged in user
    // Project should be included in each method so that this interface is agnostic of the fact that the application
    // works with just one project.
    const projectID = this.currentProjectID();
    return this.findProject(projectID);
  }

  private async findProject(projectID: ProjectID): Promise<Prisma.Project> {
    const project = await this.prisma.project.findFirst({
      where: {
        projectUID: projectID.projectUID,
      },
    });
    if (project === null) {
      throw new Error(
        `Cannot find project with projectUID ${projectID.projectUID}`
      );
    }
    return project;
  }

  async getDevices(): Promise<Device[]> {
    const project = await this.currentProject();
    const devices = await this.prisma.device.findMany({
      where: {
        project,
      },
      include: {
        fleets: true,
      },
    });
    return devices.map((device) => this.deviceFromPrismaDevice(device));
  }

  async getDeviceEvents(deviceIDs: string[]): Promise<Event[]> {
    return Promise.all(
      deviceIDs.map((deviceID) =>
        this.getEvents(IDBuilder.buildDeviceID(deviceID))
      )
    ).then((events) => events.flat());
  }

  getFleetsByProject(): Promise<any> {
    throw new Error("Method not implemented.");
  }

  getDevicesByFleet(fleetUID: string): Promise<any> {
    throw new Error("Method not implemented.");
  }

  getFleetsByDevice(deviceID: string): Promise<any> {
    throw new Error("Method not implemented.");
  }

  getDeviceEnvVars(deviceID: string): Promise<any> {
    throw new Error("Method not implemented");
  }

  getFleetEnvVars(fleetUID: string): Promise<any> {
    throw new Error("Method not implemented.");
  }

  async getEvents(deviceID: DeviceID): Promise<Event[]> {
    const deviceEvents = await this.prisma.event.findMany({
      where: {
        deviceUID: deviceID.deviceUID,
      },
    });
    return deviceEvents.map((event) => this.eventFromPrismaEvent(event));
  }

  async getDevice(deviceID: DeviceID): Promise<Device> {
    const device = await this.fetchDevice(deviceID);
    if (device === null) {
      const project = await this.currentProject();
      throw new Error(
        `Cannot find device with DeviceUID ${deviceID.deviceUID} in project ${project.projectUID}`
      );
    }
    return this.deviceFromPrismaDevice(device);
  }

  async fetchDevice(deviceID: DeviceID) {
    const device = await this.prisma.device.findUnique({
      where: {
        deviceUID: deviceID.deviceUID,
      },
      include: {
        fleets: true,
      },
    });
    return device;
  }

  deviceFromPrismaDevice(
    device: Prisma.Device & { fleets: Prisma.Fleet[] }
  ): Device {
    const fleetUIDs = device.fleets.map((fleet) => fleet.fleetUID);

    return {
      ...device,
      id: IDBuilder.buildDeviceID(device.deviceUID),
      name: device.name || "",
      locationName: device.locationName || "",
      fleetUIDs,
      lastSeenAt: device.lastSeenAt?.toISOString() || "",
    };
  }

  eventFromPrismaEvent(event: Prisma.Event): Event {
    return {
      ...event,
      id: IDBuilder.buildEventID(event.eventUID),
      deviceUID: IDBuilder.buildDeviceID(event.deviceUID),
      when: event.when.toISOString(),
      value: event.value,
    };
  }
}
