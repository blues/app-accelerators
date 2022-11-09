/* eslint-disable prefer-rest-params */
import { PrismaClient, Prisma, Project, Device, Event } from "@prisma/client";
import { ErrorWithCause } from "pony-cause";
import { serverLogError, serverLogInfo } from "../../pages/api/log";
import NotehubLocation from "../notehub/models/NotehubLocation";
import { AppEvent, AppEventHandler } from "../AppEvent";

/**
 * The "hidden" property that describes the property that bears the primary data item in the event.
 */

export default class PrismaDatastoreEventHandler implements AppEventHandler {
  constructor(private prisma: PrismaClient) {}

  /**
   * Handles an AppEvent relating to devices in the project.
   * The project is first looked up from the projectUID.
   * @param event
   * @returns
   */
  public async handleEvent(
    event: AppEvent,
    isHistorical = false
  ): Promise<void> {
    const verbose = !isHistorical;
    const verboseLog = verbose ? serverLogInfo : () => {};

    verboseLog("handling event", event);

    // todo - should we validate the project? and create on demand?
    const project = await this.projectFromNaturalKey(event.projectUID);

    // todo add fleet if fleet data is present? fix tomorrow
    if (event.fleetUID && event.fleetName) {
      const fleet = await this.upsertFleet(
        project,
        event?.fleetUID,
        event?.fleetName
      );
    }

    // todo add fleet if fleet data is present?
    const device = await this.upsertDevice(
      project,
      event.deviceUID,
      event.deviceName,
      event.when,
      event.location,
      event?.fleetUID
    );

    const deviceEvent = await this.upsertEvent(
      event.deviceUID,
      event.when,
      event.eventName,
      event.eventUID,
      event.eventBody
    );
  }

  private upsertFleet(project: Project, fleetUID?: string, fleetName?: string) {
    const args = arguments;
    return this.prisma.fleet
      .upsert({
        where: {
          fleetUID,
        },
        create: {
          fleetName,
          fleetUID,
          project: {
            connect: {
              id: project.id,
            },
          },
        },
        update: {
          fleetName,
          project: {
            connect: {
              id: project.id,
            },
          },
        },
      })
      .catch((cause) => {
        throw new ErrorWithCause(
          `error upserting fleet ${fleetUID} ${JSON.stringify(args)}`,
          { cause }
        );
      });
  }

  /**
   * Insert or update the gateway based on the unique device ID.  If the gateway exists but is in a different project,
   * the project is updated.
   *
   * @param project
   * @param deviceUID
   * @param name
   * @param lastSeenAt
   * @param location
   * @param fleetUID
   * @returns
   */
  private upsertDevice(
    project: Project,
    deviceUID: string,
    name: string | undefined,
    lastSeenAt: Date,
    location?: NotehubLocation,
    fleetUID?: string
  ) {
    const args = arguments;
    const locationName = location?.name; // todo use structured location

    return this.prisma.device
      .upsert({
        where: {
          deviceUID,
        },
        create: {
          name,
          deviceUID,
          locationName,
          fleetUID,
          project: {
            connect: {
              id: project.id,
            },
          },
          lastSeenAt,
        },
        update: {
          name,
          locationName,
          fleetUID,
          project: {
            connect: {
              id: project.id,
            },
          },
          lastSeenAt,
        },
      })
      .catch((cause) => {
        throw new ErrorWithCause(
          `error upserting device ${deviceUID} ${JSON.stringify(args)}`,
          { cause }
        );
      });
  }

  /**
   * Insert or update the event
   *
   * @param deviceUID
   * @param when
   * @param eventName
   * @param eventUID
   * @param value
   * @returns
   */
  private upsertEvent(
    deviceUID: string,
    when: Date,
    eventName: string,
    eventUID: string,
    value: string
  ) {
    const args = arguments;

    return this.prisma.event
      .upsert({
        where: {
          eventUID_when_value: {
            eventUID,
            when,
            value,
          },
        },
        create: {
          deviceUID,
          when,
          eventName,
          eventUID,
          value,
        },
        update: {
          // reading already exists
          // no-op
        },
      })
      .catch((cause) => {
        throw new ErrorWithCause(
          `error upserting event ${deviceUID} ${JSON.stringify(args)}`,
          { cause }
        );
      });
  }

  private async projectFromNaturalKey(projectUID: string) {
    const project = await this.prisma.project.findUnique({
      where: {
        projectUID,
      },
      rejectOnNotFound: true,
    });
    return project;
  }
}
