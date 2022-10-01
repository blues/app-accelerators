/* eslint-disable no-restricted-syntax */
/* eslint-disable no-await-in-loop */
/* eslint-disable import/prefer-default-export */
import Prisma, { PrismaClient } from "@prisma/client";
import { ErrorWithCause } from "pony-cause";
import GatewayDEPRECATED from "../alpha-models/Gateway";
import ReadingDEPRECATED from "../alpha-models/readings/Reading";
import NodeDEPRECATED from "../alpha-models/Node";
import {
  DataProvider,
  QueryResult,
  QueryHistoricalReadings,
  BulkImport,
} from "../DataProvider";
import {
  ProjectID,
  ProjectReadingsSnapshot,
  SensorHost,
  SensorHostReadingsSnapshot,
  SensorType,
  Reading,
  ProjectHistoricalData,
  NodeSensorTypeNames,
} from "../DomainModel";
import Mapper from "./PrismaDomainModelMapper";
import {
  serverLogError,
  serverLogInfo,
  serverLogProgress,
} from "../../pages/api/log";
import { NotehubAccessor } from "../notehub/NotehubAccessor";
import { SparrowEventHandler } from "../SparrowEvent";
import { sparrowEventFromNotehubRoutedEvent } from "../notehub/SparrowEvents";
import NotehubDataProvider from "../notehub/NotehubDataProvider";
import { gatewayTransformUpsert, nodeTransformUpsert } from "./importTransform";
import {
  NodeWithGateway,
  NodeWithLatestReadings,
  sparrowGatewayFromPrismaGateway,
  sparrowNodeFromPrismaNode,
} from "./prismaToSparrow";
import ReadingSchema from "../alpha-models/readings/ReadingSchema";
import VoltageSensorSchema from "../alpha-models/readings/VoltageSensorSchema";
import HumiditySensorSchema from "../alpha-models/readings/HumiditySensorSchema";
import TemperatureSensorSchema from "../alpha-models/readings/TemperatureSensorSchema";
import PressureSensorSchema from "../alpha-models/readings/PressureSensorSchema";
import ContactSwitchSensorSchema from "../alpha-models/readings/ContactSwitchSensorSchema";

async function manageGatewayImport(
  bi: BulkImport,
  p: PrismaClient,
  project: Prisma.Project,
  gateway: GatewayDEPRECATED
) {
  const b = bi;
  serverLogInfo("gateway import", gateway.name, gateway.uid);
  try {
    b.itemCount += 1;
    await p.gateway.upsert(gatewayTransformUpsert(gateway, project));
  } catch (cause) {
    b.errorCount += 1;
    b.itemCount -= 1;
    serverLogError(
      `Failed to import gateway "${gateway.name}": ${String(cause)}`.replaceAll(
        `\n`,
        " "
      )
    );
  }
}

async function manageNodeImport(
  bi: BulkImport,
  p: PrismaClient,
  project: Prisma.Project,
  node: NodeDEPRECATED
) {
  const b = bi;
  serverLogInfo("node import", node.name, node.nodeId, node.gatewayUID);
  try {
    b.itemCount += 1;
    await p.node.upsert(nodeTransformUpsert(node));
  } catch (cause) {
    b.errorCount += 1;
    b.itemCount -= 1;
    serverLogError(
      `Failed to import node "${String(node.name)}" (${node.nodeId}): ${String(
        cause
      )}`.replaceAll(`\n`, " ")
    );
  }
}

/**
 * Implements the DataProvider service using Prisma ORM.
 */
export class PrismaDataProvider implements DataProvider {
  // todo - passing in the project - this is too restraining and belongs in the app layer.
  // but it's like this for now since the original DataProvider interface doesn't have Project.
  // When the domain model refactor is complete, the projectUID constructor parameter can be removed.
  constructor(
    private prisma: PrismaClient,
    private projectUID: ProjectID // todo - remove
  ) {}

  async doBulkImport(
    source?: NotehubAccessor,
    target?: SparrowEventHandler
  ): Promise<BulkImport> {
    serverLogInfo("Bulk import starting");
    const b: BulkImport = { itemCount: 0, errorCount: 0 };

    if (!source)
      throw new Error("PrismaDataProvider needs a source for bulk data import");
    if (!target)
      throw new Error("PrismaDataProvider needs a target for bulk data import");

    const project = await this.currentProject();

    // Some  details have to be fetched from the notehub api (because some
    // gateway details like name are only available in environment variables)
    const notehubProvider = new NotehubDataProvider(source, {
      type: "ProjectID",
      projectUID: project.projectUID,
    });
    const gateways = await notehubProvider.getGateways();
    for (const gateway of gateways) {
      await manageGatewayImport(b, this.prisma, project, gateway);
    }

    const nodes = await notehubProvider.getNodes(gateways.map((g) => g.uid));
    for (const node of nodes) {
      await manageNodeImport(b, this.prisma, project, node);
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
          sparrowEventFromNotehubRoutedEvent(event, project.projectUID),
          isHistorical
        );
        b.itemCount += 1;
      } catch (cause) {
        const eventString = JSON.stringify(event);
        serverLogError(
          `Error loading event ${eventString}. Cause: ${String(cause)}`
        );
        b.errorCount += 1;
      }
      serverLogProgress("Loaded", events.length, i);
    }

    serverLogInfo("Bulk import complete");

    return b;
  }

  private currentProjectID(): ProjectID {
    return this.projectUID;
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

  async getGateways(): Promise<GatewayDEPRECATED[]> {
    const project = await this.currentProject();
    const gateways = await this.prisma.gateway.findMany({
      where: {
        project,
      },
      include: {
        readingSource: {
          include: { sensors: { include: { latest: true, schema: true } } },
        },
      },
    });
    return gateways.map((gw) => sparrowGatewayFromPrismaGateway(gw));
  }

  async getGateway(gatewayUID: string): Promise<GatewayDEPRECATED> {
    const gateway = await this.fetchGateway(gatewayUID);
    if (gateway === null) {
      const project = await this.currentProject();
      throw new Error(
        `Cannot find gateway with DeviceUID ${gatewayUID} in project ${project.projectUID}`
      );
    }
    return sparrowGatewayFromPrismaGateway(gateway);
  }

  async fetchGateway(gatewayUID: string) {
    const gateway = await this.prisma.gateway.findUnique({
      where: {
        deviceUID: gatewayUID,
      },
      include: {
        readingSource: {
          include: { sensors: { include: { latest: true, schema: true } } },
        },
      },
    });
    return gateway;
  }

  getNodes(gatewayUIDs: string[]): Promise<NodeDEPRECATED[]> {
    // for now just issue multiple queries. Not sure how useful this method is anyway.
    return Promise.all(
      gatewayUIDs.map((gatewayUID) => this.getGatewayNodes(gatewayUID))
    ).then((nodes) => nodes.flat());
  }

  /**
   * Retrieve the nodes for a given gatway.
   * @param gatewayUID  The ID of the gateway to retrieve.
   * @returns
   */
  async getGatewayNodes(gatewayUID: string): Promise<NodeDEPRECATED[]> {
    // todo - use a query to retrieve many nodes from the db rather than iterate if performance doesn't scale.
    const nodes = await this.prisma.node.findMany({
      where: {
        gateway: {
          deviceUID: gatewayUID,
        },
      },
      include: {
        readingSource: {
          include: { sensors: { include: { latest: true, schema: true } } },
        },
      },
    });

    return nodes.map((node) => sparrowNodeFromPrismaNode(gatewayUID, node));
  }

  async getNode(
    gatewayUID: string | null,
    sensorUID: string
  ): Promise<NodeDEPRECATED> {
    const node = await this.fetchNode(sensorUID);
    if (!node || (gatewayUID && node.gateway.deviceUID !== gatewayUID)) {
      const project = await this.currentProject();
      throw new Error(
        `Cannot find node with NodeID ${sensorUID} in project ${project.projectUID}`
      );
    }
    return sparrowNodeFromPrismaNode(node.gateway.deviceUID, node);
  }

  async fetchNode(
    nodeID: string
  ): Promise<(NodeWithGateway & NodeWithLatestReadings) | null> {
    // const project = await this.currentProject();
    // todo - constraint to the project
    const node = await this.prisma.node.findUnique({
      where: {
        nodeEUI: nodeID,
      },
      include: {
        gateway: true,
        readingSource: {
          include: { sensors: { include: { latest: true, schema: true } } },
        },
      },
    });
    return node;
  }

  async getNodeData(
    gatewayUID: string,
    sensorUID: string,
    minutesBeforeNow: number
  ): Promise<ReadingDEPRECATED<unknown>[]> {
    const from: Date = minutesBeforeNow
      ? new Date(Date.now() - minutesBeforeNow * 60000)
      : new Date(0);

    // retrieve all the readings for the given sensor
    const readings = await this.prisma.reading.findMany({
      where: {
        when: {
          gte: from,
        },
        sensor: {
          readingSource: {
            node: {
              nodeEUI: sensorUID,
            },
          },
        },
      },
      include: {
        sensor: {
          include: {
            schema: {
              select: {
                name: true,
                scale: true,
              },
            },
          },
        },
      },
    });

    const map = new Map<string, ReadingSchema<unknown>>();
    map.set(NodeSensorTypeNames.VOLTAGE, VoltageSensorSchema);
    map.set(NodeSensorTypeNames.TEMPERATURE, TemperatureSensorSchema);
    map.set(NodeSensorTypeNames.HUMIDITY, HumiditySensorSchema);
    map.set(NodeSensorTypeNames.AIR_PRESSURE, PressureSensorSchema);
    map.set(NodeSensorTypeNames.DOOR_STATUS, ContactSwitchSensorSchema);

    const result: ReadingDEPRECATED<unknown>[] = [];

    readings.forEach((reading) => {
      if (typeof reading.value !== "string") {
        const alphaSchema = map.get(reading.sensor.schema.name);
        if (alphaSchema) {
          const { scale } = reading.sensor.schema;

          const alphaReading = {
            value: scale ? Number(reading.value) / scale : reading.value,
            captured: reading.when.toISOString(),
            schema: alphaSchema,
          };
          result.push(alphaReading);
        }
      } else {
        const alphaSchema = map.get(reading.sensor.schema.name);
        if (alphaSchema) {
          const alphaReading = {
            value: reading.value,
            captured: reading.when.toISOString(),
            schema: alphaSchema,
          };
          result.push(alphaReading);
        }
      }
    });
    return result;
  }

  private retrieveLatestValues({ projectUID }: { projectUID: string }) {
    const latestReading = {
      // from the readingSource, fetch all sensors and the latest reading of each.
      include: {
        sensors: {
          include: {
            latest: true,
            schema: true,
          },
        },
      },
    };

    // this retrieves the hiearachy of project/gateway/node with the latest reading for each
    return this.prisma.project.findUnique({
      where: {
        projectUID,
      },
      include: {
        gateways: {
          include: {
            readingSource: latestReading,
            nodes: {
              include: {
                readingSource: latestReading,
              },
            },
          },
        },
      },
      rejectOnNotFound: true,
    });
  }

  async queryProjectLatestValues(
    projectID: ProjectID
  ): Promise<QueryResult<ProjectID, ProjectReadingsSnapshot>> {
    let prismaProject;
    try {
      prismaProject = await this.retrieveLatestValues(projectID);
    } catch (cause) {
      throw new ErrorWithCause("Error getting latest values from database.", {
        cause,
      });
    }

    // get the types indirectly so loose coupling
    type P = typeof prismaProject;
    type G = P["gateways"][number];
    type N = G["nodes"][number];
    type RS = G["readingSource"];

    // map the data to the domain model
    const hostReadings = new Map<SensorHost, SensorHostReadingsSnapshot>();

    /**
     * Walks the sensors associated with a ReadingSource, and converts the ReadingSchema and Reading to
     * SensorType and Reading.
     * @param rs
     * @param sensorHost
     */
    const addReadingSource = (rs: RS, sensorHost: SensorHost) => {
      // one reading per sensor type
      const readings = new Map<SensorType, Reading>();

      const snapshot: SensorHostReadingsSnapshot = {
        sensorHost,
        sensorTypes: new Map(),
        readings,
      };

      // maydo - could consider caching the ReadingSchema -> SensorType but it's not that much overhead with duplication per device
      rs.sensors.forEach((s) => {
        if (s.latest) {
          const sensorType = Mapper.mapReadingSchema(s.schema);
          const reading = Mapper.mapReading(s.latest);

          snapshot.sensorTypes.set(sensorType.name, sensorType);
          readings.set(sensorType, reading);
        }
      });

      hostReadings.set(sensorHost, snapshot);
    };

    const deepMapNode = (n: N) => {
      const result = Mapper.mapNode(n);
      addReadingSource(n.readingSource, result);
      return result;
    };

    const deepMapGateway = (g: G) => {
      // map nodes from Prisma to DomainModel
      const nodes = new Set(g.nodes.map(deepMapNode));
      const result = Mapper.mapGatewayWithNodes(g, nodes);
      // add the reading source to provide the set of SensorType and Readings.
      addReadingSource(g.readingSource, result);
      return result;
    };

    // transform Prisma to DomainModel gateways
    const gateways = new Set(prismaProject.gateways.map(deepMapGateway));
    const project = Mapper.mapProjectHierarchy(prismaProject, gateways);

    const results: ProjectReadingsSnapshot = {
      when: Date.now(),
      project,
      hostReadings: (sensorHost: SensorHost) => {
        const reading = hostReadings.get(sensorHost);
        if (reading === undefined) {
          throw new Error("unknown sensorHost");
        }
        return reading;
      },
      hostReadingByName: (sensorHost: SensorHost, readingName: string) => {
        const snapshot = hostReadings.get(sensorHost);
        const sensorType = snapshot?.sensorTypes.get(readingName);
        return sensorType && snapshot?.readings.get(sensorType);
      },
    };

    return {
      request: projectID,
      results,
    };
  }

  async queryProjectReadingCount(
    projectID: ProjectID
  ): Promise<QueryResult<ProjectID, number>> {
    const count = await this.prisma.reading.count();
    return {
      request: projectID,
      results: count,
    };
  }

  // eslint-disable-next-line class-methods-use-this
  queryProjectReadingSeries(
    // eslint-disable-next-line @typescript-eslint/no-unused-vars
    query: QueryHistoricalReadings
  ): Promise<QueryResult<QueryHistoricalReadings, ProjectHistoricalData>> {
    throw new Error("Method not implemented.");
  }

  async gatewayWithNode(nodeId: string): Promise<GatewayDEPRECATED | null> {
    const node = await this.fetchNode(nodeId);
    return node && node?.gateway
      ? sparrowGatewayFromPrismaGateway(node.gateway)
      : null;
  }
}
