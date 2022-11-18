/* eslint-disable import/no-named-as-default */
/* eslint-disable class-methods-use-this */
/* eslint-disable no-restricted-syntax */
/* eslint-disable no-await-in-loop */
/* eslint-disable import/prefer-default-export */
import Prisma, { PrismaClient } from "@prisma/client";
import { some } from "lodash";
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
  const filteredEvents = eventList.filter(
    (event) => event.deviceUID.deviceUID === device.id.deviceUID
  );
  return filteredEvents;
}

// todo make notifications a domain object?
function filterAlarmsByDevice(device: Device, alarmList: any) {
  const filteredAlarms = alarmList.filter((alarm) =>
    alarm.content.includes(device.id.deviceUID)
  );

  return filteredAlarms;
}

function assembleDeviceEventsObject(
  device: Device,
  latestDeviceEvent: Event[],
  alarmNotification: any
) {
  // there will only ever be one item in the latestDeviceEvent array
  const updatedValveDeviceMonitorObj = {
    deviceID: device.id.deviceUID,
    name: device.name ? device.name : device.id.deviceUID,
    lastActivity: latestDeviceEvent[0].when,
    valveState: latestDeviceEvent[0].value.valve_state,
    deviceFlowRate: latestDeviceEvent[0].value.flow_rate,
    deviceAlarm: !!alarmNotification.length,
  };

  return updatedValveDeviceMonitorObj;
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
    let valveMonitorDevices: ValveMonitorDevice[] = [];

    // fetch all devices by project
    const devices = await this.getDevices();

    // fetch most most recent data.qo event for each device
    const deviceEvents = await Promise.all(
      devices.map((device) => this.getLatestDeviceEvent(device.id)).flat()
    );

    // fetch latest unacknowledged alarm.qo events for each device (if they exist)
    const deviceAlarms = await Promise.all(
      devices.map((device) => this.getLatestDeviceAlarm(device.id)).flat()
    );

    // combine data for each device and its latest event
    // todo clean this up
    valveMonitorDevices = devices.map((device) => {
      const filteredEventsByDevice = filterEventsByDevice(device, deviceEvents);
      let filteredAlarmsByDevice = [];
      if (some(deviceAlarms, !undefined)) {
        filteredAlarmsByDevice = filterAlarmsByDevice(device, deviceAlarms);
      }
      const updatedValveDeviceMonitorObj = assembleDeviceEventsObject(
        device,
        filteredEventsByDevice,
        filteredAlarmsByDevice
      );

      return updatedValveDeviceMonitorObj;
    });

    return valveMonitorDevices;
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

  async getLatestDeviceEvent(deviceID: DeviceID): Promise<Event> {
    const latestDeviceEvent = await this.prisma.event.findMany({
      where: {
        AND: {
          deviceUID: deviceID.deviceUID,
        },
        eventName: "data.qo",
      },
      take: -1,
    });
    return this.eventFromPrismaEvent(latestDeviceEvent[0]);
  }

  // todo possibly transform this returned obj like we do for events and devices?
  async getLatestDeviceAlarm(deviceID: DeviceID): Promise<any> {
    const latestAlarmFromDevice = await this.prisma.notification.findMany({
      where: {
        AND: {
          type: "alarm.qo",
        },
        content: {
          path: ["deviceID"],
          equals: deviceID.deviceUID,
        },
      },
      take: -1,
    });
    return latestAlarmFromDevice[0];
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
