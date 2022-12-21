/* eslint-disable import/no-named-as-default */
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
  Event,
  Fleets,
  FleetEnvVars,
  DeviceEnvVars,
} from "../DomainModel";
import { FlowRateMonitorConfig, FlowRateMonitorDevice } from "../AppModel";

import IDBuilder from "../IDBuilder";
import { Notification } from "../NotificationsStore";

function filterEventsByDevice(device: Device, eventList: Event[]) {
  const filteredEvents = eventList.filter(
    (event) => event.deviceUID.deviceUID === device.id.deviceUID
  );
  return filteredEvents;
}

function filterAlarmsByDevice(device: Device, alarmList: Notification[]) {
  const filteredAlarms = alarmList.filter(
    // eslint-disable-next-line @typescript-eslint/no-unsafe-member-access
    (alarm) => alarm.content?.deviceID === device.id.deviceUID
  );

  return filteredAlarms;
}

function assembleDeviceEventsObject(
  device: Device,
  latestDeviceEvent: Event[],
  alarmNotification: Notification[]
) {
  // there will only ever be one item in the latestDeviceEvent array
  const updatedFlowRateDeviceMonitorObj = {
    deviceID: device.id.deviceUID,
    name: device.name ? device.name : device.id.deviceUID,
    lastActivity: latestDeviceEvent[0].when ? latestDeviceEvent[0].when : "-",
    deviceFlowRate: latestDeviceEvent[0].value.flow_rate
      ? Number(latestDeviceEvent[0].value.flow_rate).toFixed(1)
      : "-",
    deviceAlarm: !!alarmNotification.length,
    deviceAlarmReason: alarmNotification[0]?.content?.content?.reason,
    deviceFleetID: device.fleetUIDs[0], // a device can only be assigned to one fleet a time
  };

  return updatedFlowRateDeviceMonitorObj;
}

/**
 * Implements the DataProvider service using Prisma ORM.
 */
export class PrismaDataProvider implements DataProvider {
  constructor(private prisma: PrismaClient, private projectID: ProjectID) {}

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

  async getFlowRateMonitorDeviceData(): Promise<FlowRateMonitorDevice[]> {
    let flowRateMonitorDevices: FlowRateMonitorDevice[] = [];

    // fetch all devices by project
    const devices = await this.getDevices();

    // fetch the most recent data.qo event for each device
    const deviceEvents = await Promise.all(
      devices
        .map((device) => this.getLatestDeviceEvent(device.id, "data.qo"))
        .flat()
    );

    // fetch the most recent data.qi event for each device
    const controlEvents = await Promise.all(
      devices
        .map((device) => this.getLatestDeviceEvent(device.id, "data.qi"))
        .flat()
    );

    // fetch latest unacknowledged alarm.qo events for each device (if they exist)
    const deviceAlarms = await Promise.all(
      devices.map((device) => this.getLatestDeviceAlarm(device.id)).flat()
    );

    // combine data for each device and its latest event
    flowRateMonitorDevices = devices.map((device) => {
      const filteredEventsByDevice = filterEventsByDevice(device, deviceEvents);

      const filteredAlarmsByDevice = filterAlarmsByDevice(
        device,
        deviceAlarms.filter((alarm) => alarm !== undefined)
      );

      const updatedFlowRateDeviceMonitorObj = assembleDeviceEventsObject(
        device,
        filteredEventsByDevice,
        filteredAlarmsByDevice
      );

      return updatedFlowRateDeviceMonitorObj;
    });

    return flowRateMonitorDevices;
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

  getFlowRateMonitorConfig(fleetUID: string): Promise<FlowRateMonitorConfig> {
    throw new Error("Method not implemented.");
  }

  async getLatestDeviceEvent(deviceID: DeviceID, file: string): Promise<Event> {
    const latestDeviceEvent = await this.prisma.event.findMany({
      where: {
        AND: {
          deviceUID: deviceID.deviceUID,
        },
        eventName: file,
      },
      take: -1,
    });
    if (latestDeviceEvent.length > 0) {
      return this.eventFromPrismaEvent(latestDeviceEvent[0]);
    }
    // for the rare occasion when a device is online but has generated no event data yet
    return this.noEventsFromPrisma(deviceID);
  }

  async getLatestDeviceAlarm(deviceID: DeviceID): Promise<Notification> {
    const latestAlarmFromDevice = await this.prisma.notification.findFirst({
      where: {
        AND: {
          type: "alarm",
        },
        content: {
          path: ["deviceID"],
          equals: deviceID.deviceUID,
        },
      },
      orderBy: {
        when: "desc",
      },
    });

    return latestAlarmFromDevice || undefined;
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

  // handles the odd edge case when a device is online but hasn't generated any events data yet
  noEventsFromPrisma(deviceID: DeviceID): any {
    return {
      deviceUID: IDBuilder.buildDeviceID(deviceID.deviceUID),
      value: "No events generated by device yet",
    };
  }
}
