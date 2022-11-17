/* eslint-disable class-methods-use-this */
/* eslint-disable no-restricted-syntax */
/* eslint-disable no-await-in-loop */
/* eslint-disable import/prefer-default-export */
import Prisma, { PrismaClient } from "@prisma/client";
import { DataProvider } from "../DataProvider";
import {
  ProjectID,
  DeviceID,
  Device,
  Project,
  Event,
  Fleets,
  FleetEnvVars,
  DeviceEnvVars,
} from "../DomainModel";
import { ValveMonitorDevice } from "../AppModel";

import IDBuilder from "../IDBuilder";

function filterEventsByDevice(device: Device, eventList: Event[]) {
  // consider also filtering out certain event types (like alarms or notification events) as well to keep the data cleaner and more uniform
  console.log(device.id.deviceUID, eventList);
  const filteredEvents = eventList.filter(
    (event) => event.deviceUID.deviceUID === device.id.deviceUID
  );
  console.log("Filtered events", filteredEvents);
  return filteredEvents;
}

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

  async getValveMonitorDeviceData(): Promise<ValveMonitorDevice[]> {
    const valveMonitorDevices: ValveMonitorDevice[] = [];
    const formattedValveMonitorDeviceData: ValveMonitorDevice[] = [];

    // fetch all devices by project
    const devices = await this.getDevices();
    // console.log("DEVICES--------", devices);

    // fetch most most recent event for each device
    // todo fix this tomorrow morning
    const deviceEvents = await this.getLatestDeviceEvent(
      devices.map((device) => device.id.deviceUID)
    );
    console.log("DEVICE EVENTS", deviceEvents);

    // combine data
    const valveDeviceMonitorData = devices.map((device) => {
      const filteredEventsByDevice = filterEventsByDevice(device, deviceEvents);
      // console.log("FILTERED EVENTS BY DEVICE", filteredEventsByDevice);

      // todo make this a separate helper function
      const updatedValveDeviceMonitorObj = {
        deviceID: device.id.deviceUID,
        name: device.name,
        lastActivity: filteredEventsByDevice[0].when,
        valveState: filteredEventsByDevice[0].value.valve_state,
        flowRate: filteredEventsByDevice[0].value.flow_rate,
      };
    });

    return null;
  }

  async getDeviceEvents(deviceIDs: string[]): Promise<Event[]> {
    return Promise.all(
      deviceIDs.map((deviceID) =>
        this.getEvents(IDBuilder.buildDeviceID(deviceID))
      )
    ).then((events) => events.flat());
  }

  getFleetsByProject(): Promise<Fleets> {
    throw new Error("Method not implemented.");
  }

  getDevicesByFleet(fleetUID: string): Promise<Device[]> {
    throw new Error("Method not implemented.");
  }

  getFleetsByDevice(deviceID: string): Promise<Fleets> {
    throw new Error("Method not implemented.");
  }

  getDeviceEnvVars(deviceID: string): Promise<DeviceEnvVars> {
    throw new Error("Method not implemented");
  }

  getFleetEnvVars(fleetUID: string): Promise<FleetEnvVars> {
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

  async getLatestDeviceEvent(deviceID: DeviceID): Promise<Event[]> {
    const latestDeviceEvent = await this.prisma.event.findMany({
      where: {
        AND: {
          deviceUID: deviceID.deviceUID,
        },
        eventName: "data.qo",
      },
      take: -1,
    });
    return latestDeviceEvent.map((event) => this.eventFromPrismaEvent(event));
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
