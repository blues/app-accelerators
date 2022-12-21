import * as DomainModel from "./DomainModel";
import * as AppModel from "./AppModel";
import App from "next/app";

/**
 * Knows how to convert between the types of domain model and an appropriate application model.
 */
export interface AppModelBuilder {
  /**
   * Builds the app model for a snapshot of the project.
   * All gateways and nodes are retrieved with all sensor values.
   * @param snapshot
   */
  buildProjectReadingsSnapshot(
    snapshot: DomainModel.ProjectReadingsSnapshot
  ): AppModel.Project;

  buildProjectReadingsHistory(
    history: DomainModel.ProjectHistoricalData
  ): AppModel.Project;
}

interface Visitor {
  visitProject(app: AppModel.Project, domain: DomainModel.Project): typeof app;
  visitNode(app: AppModel.Node, domain: DomainModel.Node): typeof app;
  visitGateway(app: AppModel.Gateway, domain: DomainModel.Gateway): typeof app;
}

class ReadingsSnapshotBuilderVisitor implements Visitor {
  constructor(
    private readonly builder: DomainAppModelBuilder,
    private readonly snapshot: DomainModel.ProjectReadingsSnapshot
  ) {}

  visitProject(app: AppModel.Project, domain: DomainModel.Project): typeof app {
    return app;
  }

  visitNode(app: AppModel.Node, domain: DomainModel.Node): typeof app {
    return this.buildSnapshotReadings(app, domain);
  }

  visitGateway(app: AppModel.Gateway, domain: DomainModel.Gateway): typeof app {
    return this.buildSnapshotReadings(app, domain);
  }

  private buildSnapshotReadings<Host extends AppModel.SensorHost>(
    app: Host,
    domain: DomainModel.SensorHost
  ) {
    const hostSnapshot = this.snapshot.hostReadings(domain);
    app.currentReadings = this.buildCurrentReadings(hostSnapshot);
    return app;
  }

  buildCurrentReadings(
    domain: DomainModel.SensorHostReadingsSnapshot
  ): AppModel.SensorTypeCurrentReading[] {
    const result: AppModel.SensorTypeCurrentReading[] = [];
    domain.sensorTypes.forEach((sensorType) => {
      const typeAndReading = {
        sensorType: this.builder.buildSensorType(sensorType),
        reading: this.builder.buildReading(domain.readings.get(sensorType)),
      };
      result.push(typeAndReading);
    });
    return result;
  }
}

class DomainAppModelBuilder implements AppModelBuilder {
  buildProjectReadingsHistory(
    history: DomainModel.ProjectHistoricalData
  ): AppModel.Project {
    throw new Error("Method not implemented.");
  }

  buildProjectReadingsSnapshot(
    snapshot: DomainModel.ProjectReadingsSnapshot
  ): AppModel.Project {
    const visitor = new ReadingsSnapshotBuilderVisitor(this, snapshot);
    const project: AppModel.Project = this.buildProject(
      snapshot.project,
      visitor
    );
    project.gateways = this.buildHierarchy(snapshot.project.gateways, visitor);
    return project;
  }

  buildHierarchy(
    gateways: Set<DomainModel.GatewayWithNodes>,
    visitor: Visitor
  ): AppModel.Gateway[] {
    const result = this.buildGateways(gateways, visitor);
    let index = 0;
    gateways.forEach((g) => {
      const build = result[index++];
      build.nodes = this.buildNodes(g.nodes, visitor);
    });

    return result;
  }

  buildNodes(nodes: DomainModel.Nodes, visitor: Visitor): AppModel.Node[] {
    const result: AppModel.Node[] = [];
    nodes.forEach((node) => {
      result.push(this.buildNode(node, visitor));
    });
    return result;
  }

  buildNode(node: DomainModel.Node, visitor: Visitor): AppModel.Node {
    return visitor.visitNode(
      {
        id: node.id,
        name: node.name,
        lastSeen: this.buildDate(node.lastSeen),
        descriptionBig: node.descriptionBig,
        descriptionSmall: node.descriptionSmall,
        locationName: node.locationName,
        historicalReadings: null,
        currentReadings: null,
      },
      node
    );
  }

  private buildProject(
    project: DomainModel.Project,
    visitor: Visitor
  ): AppModel.Project {
    return visitor.visitProject(
      {
        id: project.id,
        name: project.name,
        description: project.description,
        gateways: null,
      },
      project
    );
  }

  private buildGateways(
    gateways: DomainModel.Gateways,
    visitor: Visitor
  ): AppModel.Gateway[] {
    const result: AppModel.Gateway[] = [];
    gateways.forEach((g) => {
      result.push(this.buildGateway(g, visitor));
    });
    return result;
  }

  private buildGateway(
    gateway: DomainModel.Gateway,
    visitor: Visitor
  ): AppModel.Gateway {
    return visitor.visitGateway(
      {
        id: gateway.id,
        name: gateway.name,
        lastSeen: this.buildDate(gateway.lastSeen),
        locationName: gateway.locationName,
        descriptionBig: gateway.descriptionBig,
        descriptionSmall: gateway.descriptionSmall,
        nodes: null,
        currentReadings: null,
        historicalReadings: null,
      },
      gateway
    );
  }

  private buildDate(
    date: DomainModel.DomainDate | null
  ): AppModel.AppDate | null {
    if (date == null) return null;
    // for now they are the same, both numbers
    // const result: number = date.getTime() / 1000;
    return date;
  }

  buildSensorType(domain: DomainModel.SensorType): AppModel.SensorType {
    // for now the domain and app models are in sync
    return domain;
  }

  buildReading(
    reading: DomainModel.Reading | undefined
  ): AppModel.Reading | null {
    return reading == undefined ? null : reading;
  }
}

const builder: AppModelBuilder = new DomainAppModelBuilder();

export default builder;
