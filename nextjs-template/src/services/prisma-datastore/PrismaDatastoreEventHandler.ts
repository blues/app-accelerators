/* eslint-disable prefer-rest-params */
import { PrismaClient, Project } from "@prisma/client";
import { ErrorWithCause } from "pony-cause";
import { serverLogInfo } from "../../pages/api/log";
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

    const device = await this.upsertDevice(
      project,
      event.deviceUID,
      event.deviceName,
      event.when,
      event.fleetUIDs,
      event.location
    );

    const deviceEvent = await this.upsertEvent(
      event.deviceUID,
      event.when,
      event.eventName,
      event.eventUID,
      event.eventBody
    );
  }

  /**
   * Insert or update the gateway based on the unique device ID.  If the gateway exists but is in a different project,
   * the project is updated.
   *
   * @param project
   * @param deviceUID
   * @param name
   * @param lastSeenAt
   * @param fleetUIDs
   * @param location
   * @returns
   */
  private upsertDevice(
    project: Project,
    deviceUID: string,
    name: string | undefined,
    lastSeenAt: Date,
    fleetUIDs: string[],
    location?: NotehubLocation
  ) {
    const args = arguments;
    const locationName = location?.name; // todo use structured location

    const formatConnectedFleetData = fleetUIDs.map((fleet) => ({
      create: {
        fleetUID: fleet,
        projectID: project.id,
      },
      where: {
        fleetUID: fleet,
      },
    }));

    return this.prisma.device
      .upsert({
        where: {
          deviceUID,
        },
        create: {
          name,
          deviceUID,
          locationName,
          fleets: {
            connectOrCreate: formatConnectedFleetData,
          },
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
          fleets: {
            deleteMany: {},
            connectOrCreate: formatConnectedFleetData,
          },
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
