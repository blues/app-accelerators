/* eslint-disable prefer-rest-params */
import {
  PrismaClient,
  Prisma,
  ReadingSource,
  ReadingSchema,
  ReadingSchemaValueType,
  Project,
  Sensor,
  ReadingSourceType,
  Gateway,
  Reading,
} from "@prisma/client";
import _ from "lodash";
import { ErrorWithCause } from "pony-cause";
import { serverLogError, serverLogInfo } from "../../pages/api/log";
import NotehubLocation from "../notehub/models/NotehubLocation";
import { SparrowEvent, SparrowEventHandler } from "../SparrowEvent";

/**
 * The "hidden" property that describes the property that bears the primary data item in the event.
 */
// todo - move to a shared definition. Also used in db-init.ts
const __primary = "__primary";

type SensorWithSchema = Sensor & { schema: ReadingSchema };

export default class PrismaDatastoreEventHandler
  implements SparrowEventHandler
{
  constructor(private prisma: PrismaClient) {}

  /**
   * Handles a SparrowEvent relating to a gateway or a node.
   * The project is first looked up from the projectUID.
   * @param event
   * @returns
   */
  public async handleEvent(
    event: SparrowEvent,
    isHistorical = false
  ): Promise<void> {
    const verbose = !isHistorical;
    const verboseLog = verbose ? serverLogInfo : () => {};

    verboseLog("handling event", event);

    // todo - should we validate the project? and create on demand?
    const project = await this.projectFromNaturalKey(event.projectUID);

    const gateway = await this.upsertGateway(
      project,
      event.gatewayUID,
      event.gatewayName,
      event.when
    );
    const node = event.nodeID
      ? await this.upsertNode(project, gateway, event.nodeID, event.when)
      : undefined;

    // the schema can exist at the node, gateway, project or global level.
    // todo - add global reading source in db-init.ts
    const deviceReadingSources = [gateway.readingSource, project.readingSource];
    if (node) {
      deviceReadingSources.push(node.readingSource);
    }
    const deviceReadingSource = node
      ? node.readingSource
      : gateway.readingSource;

    // find the schemata that matches the event name and reading source
    const schemas = await this.prisma.readingSchema.findMany({
      where: {
        eventName: event.eventName,
        reading_source_id: {
          in: deviceReadingSources.map((rs) => rs.id),
        },
      },
    });

    // upsert the sensors corresponding to the device reading source and the matched schemata.
    const sensors = await this.upsertSensors(deviceReadingSource, schemas);

    // todo - the whole event body is stored for each reading on multiple schema. ideally the schema filters the
    // event body to include only the relevant data. Optimization only.
    const promises = sensors.map((sensor) =>
      this.addSensorReading(
        sensor,
        event.when,
        event.eventBody as Prisma.InputJsonValue
      )
    );
    return Promise.all(promises)
      .then((r) => {
        verboseLog("added readings", r);
      })
      .catch((reason) => serverLogError(`Failed to add readings: ${reason}`));
  }

  private async sensorsForDeviceSchema(
    deviceReadingSource: ReadingSource,
    schemas: ReadingSchema[]
  ) {
    return this.prisma.sensor.findMany({
      where: {
        readingSource: deviceReadingSource,
        schema_id: {
          in: schemas.map((s) => s.id),
        },
      },
      include: {
        schema: true,
      },
    });
  }

  /**
   * Create or retrieve sensors for the device.
   * @param deviceReadingSource
   * @param schemas
   */
  private async upsertSensors(
    deviceReadingSource: ReadingSource,
    schemas: ReadingSchema[]
  ): Promise<SensorWithSchema[]> {
    // prisma doesn't currently support multiple upserts.  Using a simple iterative approach.
    // Almost all of the time the sensors already exist

    let existingSensors = await this.sensorsForDeviceSchema(
      deviceReadingSource,
      schemas
    );
    if (existingSensors.length !== schemas.length) {
      serverLogInfo("adding missing sensors");
      const sensorForSchema = new Map<number, Sensor>(); // map reading schema ID to sensor
      existingSensors.forEach((s) => {
        sensorForSchema.set(s.schema.id, s);
      });

      // there are some sensors that need creating
      const toCreate = schemas.filter((s) => !sensorForSchema.has(s.id));
      await this.prisma.sensor.createMany({
        data: toCreate.map((schema) => ({
          schema_id: schema.id,
          reading_source_id: deviceReadingSource.id,
        })),
      });
      existingSensors = await this.sensorsForDeviceSchema(
        deviceReadingSource,
        schemas
      );
      // todo - check that it has the expected size?
    }
    return existingSensors;
  }

  private addSensorReading(
    sensor: SensorWithSchema,
    when: Date,
    value: Prisma.InputJsonValue
  ): Promise<Reading | "skipped"> {
    const { schema } = sensor;
    let storedValue = value;
    const primaryValue = (schema.spec as { [__primary]: string })[__primary];
    if (primaryValue) {
      switch (schema.valueType) {
        case ReadingSchemaValueType.SCALAR_INT:
          storedValue = (value as any)[primaryValue] as number;
          break;
        case ReadingSchemaValueType.SCALAR_FLOAT:
          storedValue = (value as any)[primaryValue] as number;
          break;
        case ReadingSchemaValueType.STRING:
          storedValue = (value as any)[primaryValue] as string;
          break;
        case ReadingSchemaValueType.COMPOSITE:
        default:
          storedValue = value;
          break;
      }
    }

    if (!storedValue) {
      if (schema.eventName === "_session.qo") {
        // It's known that _session.qo shows values when routed that aren't
        // shown when fetched via API. If this event came from the API then
        // it might have missing values. So let's be quiet about it.
        return Promise.resolve("skipped");
      }
      // check that the value meets the prerequisites. This acts as a filter
      if (
        typeof schema?.prereq !== "object" ||
        typeof value !== "object" ||
        !_.isEqual({ ...schema.prereq, ...value }, value)
      ) {
        return Promise.resolve("skipped");
      }
      throw new Error(
        `Could not find primary value ${primaryValue} to store for sensor ${
          sensor.schema.name
        } with value ${JSON.stringify(value)} at ${when}`
      );
    }

    // update the latest reading. todo - this assumes readings are received in
    // order. Check that the new reading is more recent than the existing one.

    return this.prisma.reading.upsert({
      where: {
        sensor_id_when_value: {
          sensor_id: sensor.id,
          when,
          value: storedValue,
        },
      },
      create: {
        sensor: { connect: { id: sensor.id } },
        latest: { connect: { id: sensor.id } },
        when,
        value: storedValue,
      },
      update: {
        // reading already exists
        // no-op
      },
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
   * @returns
   */
  private upsertGateway(
    project: Project,
    deviceUID: string,
    gatewayName: string | undefined,
    lastSeenAt: Date,
    location?: NotehubLocation
  ) {
    const args = arguments;
    const name = gatewayName;
    const locationName = location?.name; // todo use structured location

    return this.prisma.gateway
      .upsert({
        where: {
          deviceUID,
        },
        include: {
          readingSource: true,
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
          readingSource: {
            create: {
              type: ReadingSourceType.GATEWAY,
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
          `error upserting gateway ${deviceUID} ${JSON.stringify(args)}`,
          { cause }
        );
      });
  }

  private upsertNode(
    project: Project,
    gateway: Gateway,
    nodeID: string,
    lastSeenAt: Date
  ) {
    const args = arguments;
    return this.prisma.node
      .upsert({
        where: {
          nodeEUI: nodeID,
        },
        include: {
          readingSource: true,
        },
        create: {
          nodeEUI: nodeID,
          gateway: {
            connect: {
              id: gateway.id,
            },
          },
          lastSeenAt,
          readingSource: {
            create: {
              type: ReadingSourceType.NODE,
            },
          },
        },
        update: {
          gateway: {
            connect: {
              id: gateway.id,
            },
          },
          lastSeenAt,
        },
      })
      .catch((cause) => {
        throw new ErrorWithCause(`error updating node ${nodeID} ${args}`, {
          cause,
        });
      });
  }

  private async projectFromNaturalKey(projectUID: string) {
    const project = await this.prisma.project.findUnique({
      where: {
        projectUID,
      },
      include: {
        readingSource: true,
      },
      rejectOnNotFound: true,
    });
    return project;
  }
}
