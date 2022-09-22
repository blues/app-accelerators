/* eslint-disable no-restricted-syntax */
/* eslint-disable no-await-in-loop */
/* eslint-disable import/prefer-default-export */
import Prisma, { PrismaClient } from "@prisma/client";
import { DataProvider, BulkImport } from "../DataProvider";
import { ProjectID, DeviceID, Device, Project } from "../DomainModel";
import {
  serverLogError,
  serverLogInfo,
  serverLogProgress,
} from "../../pages/api/log";
import { NotehubAccessor } from "../notehub/NotehubAccessor";
import { AppEventHandler } from "../AppEvent";
import NotehubDataProvider from "../notehub/NotehubDataProvider";
import { deviceTransformUpsert } from "./importTransform";

import IDBuilder from "../IDBuilder";
import { DeviceTracker, TrackerConfig } from "../ClientModel";
import { appEventFromNotehubRoutedEvent } from "../notehub/AppEvents";

async function manageDeviceImport(
  bi: BulkImport,
  p: PrismaClient,
  project: Prisma.Project,
  device: Device
) {
  const b = bi;
  serverLogInfo("device import", device.name, device.id.deviceUID);
  try {
    b.itemCount += 1;
    await p.device.upsert(deviceTransformUpsert(device, project));
  } catch (cause) {
    b.errorCount += 1;
    b.itemCount -= 1;
    serverLogError(
      `Failed to import device "${device.name}": ${String(cause)}`.replaceAll(
        `\n`,
        " "
      )
    );
  }
}

/**
 * Implements the DataProvider service using Prisma ORM.
 */
export class PrismaDataProvider implements DataProvider {
  constructor(private prisma: PrismaClient, private projectID: ProjectID) {}
  getTrackerConfig(): Promise<TrackerConfig> {
    throw new Error("Method not implemented.");
  }

  async getProject(): Promise<Project> {
    const project = await this.currentProject();
    return {
      id: this.projectID,
      name: project.name,
      description: null,
    };
  }

  async doBulkImport(
    source?: NotehubAccessor,
    target?: AppEventHandler
  ): Promise<BulkImport> {
    serverLogInfo("Bulk import starting");
    const b: BulkImport = { itemCount: 0, errorCount: 0 };

    if (!source)
      throw new Error("PrismaDataProvider needs a source for bulk data import");
    if (!target)
      throw new Error("PrismaDataProvider needs a target for bulk data import");

    const project = await this.currentProject();

    // Some  details have to be fetched from the notehub api (because some
    // device details like name are only available in environment variables)
    const notehubProvider = new NotehubDataProvider(
      source,
      {
        type: "ProjectID",
        projectUID: project.projectUID,
      },
      { type: "FleetID", fleetUID: "" }
    );
    const devices = await notehubProvider.getDevices();
    for (const device of devices) {
      await manageDeviceImport(b, this.prisma, project, device);
    }

    const now = new Date(Date.now());
    const pilotBulkImportDays = 10;
    const hoursBack = 24 * pilotBulkImportDays;
    const startDate = new Date(now);
    startDate.setUTCHours(now.getUTCHours() - hoursBack);

    serverLogInfo(`Loading events since ${startDate}`);
    const startDateAsString = `${Math.round(startDate.getTime() / 1000)}`;

    const events = await source.getEvents(startDateAsString);

    const isHistorical = true;
    let i = 0;
    for (const event of events) {
      i += 1;
      try {
        await target.handleEvent(
          appEventFromNotehubRoutedEvent(event),
          isHistorical
        );
        b.itemCount += 1;
      } catch (cause) {
        serverLogError(
          `Error loading event ${event.event}. Cause: ${String(cause)}`
        );
        b.errorCount += 1;
      }
      serverLogProgress("Loaded", events.length, i);
    }

    serverLogInfo("Bulk import complete");

    return b;
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
    });
    return devices.map((device) => this.deviceFromPrismaDevice(device));
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
    });
    return device;
  }

  deviceFromPrismaDevice(device: Prisma.Device): Device {
    throw new Error("Method not implemented.");
  }

  async getDeviceTrackerData(): Promise<DeviceTracker[]> {
    throw new Error("Method not implemented.");
  }
}
