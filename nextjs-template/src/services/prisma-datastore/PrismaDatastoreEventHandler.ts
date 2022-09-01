/* eslint-disable prefer-rest-params */
import {
  PrismaClient,
  Prisma,
  Project,
  Device
} from "@prisma/client";
import { ErrorWithCause } from "pony-cause";
import { serverLogError, serverLogInfo } from "../../pages/api/log";
import NotehubLocation from "../notehub/models/NotehubLocation";
import { AppEvent, AppEventHandler } from "../AppEvent";

/**
 * The "hidden" property that describes the property that bears the primary data item in the event.
 */

export default class PrismaDatastoreEventHandler
  implements AppEventHandler
{
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
      event.when
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
   * @param location
   * @returns
   */
  private upsertDevice(
    project: Project,
    deviceUID: string,
    name: string | undefined,
    lastSeenAt: Date,
    location?: NotehubLocation
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
