/**
 * This module manages registration of renderers for a given sensor type.
 *
 * some concepts:
 * renderers are optional - the system will try to do the best it can. simple numeric values and strings are
 * rendered. Complex types are rendered as a composite that renders name/value pairs 
 * todo - extend `spec` to describe sensorType for each element in a complex type?
 * probably easier to define a group that the readings are in, and these are handled together as a composite.

 a sensor reading - a single reading. Renderings: card/tile, tablular,
 a series of readings - multiple readings. Renderings: chart, sparkline, aggregate?


future concepts:
* reading groups - how to group multiple distinct values into a group (e.g. motion count/total)
    should this simply be done at the event level - the event groups the data the presentation can then display a group and subvalues.

    
    This code runs entirely in the client
 */

import {
  Gateway,
  Node,
  SensorType,
  Reading,
  ReadingSeries,
} from "../../services/AppModel";

export type TargetSensorHost = {
  readonly node: Node | null;
  readonly gateway: Gateway;

  // todo - add other readings in context to allow for composite renderers that render multiple values.
};

/**
 * All the context required to render a single reading.
 */
export type RenderSensorReadingProps = {
  readonly sensorType: SensorType;
  readonly reading: Reading | null; // optional because a given sensorType may not have a reading yet
} & TargetSensorHost;

/**
 * All the context required to render a series of readings
 */
export type RenderSensorReadingSeriesProps = {
  readonly sensorType: SensorType;
  readonly readingSeries: ReadingSeries;
} & TargetSensorHost;

/**
 * The expected signature for a reading renderer.
 */
export type SensorReadingRenderer = (
  props: RenderSensorReadingProps
) => JSX.Element | null;
export type SensorReadingSeriesRenderer = (
  props: RenderSensorReadingSeriesProps
) => JSX.Element | null;

/**
 * The scope that a registration applies to.
 * This allows a renderer to apply to the type of measurement made by a sensor (and apply to all sensors
 * that take that measurement, or to a more specific sensor type.
 */
export const enum RegistrationScope {
  /**
   * Register the renderer for a specific named sensor type.
   */
  NAME,

  /**
   * Register the renderer for a class of sensor types with the same measurement.
   */
  MEASURE,
}

export const enum ReadingVisualization {
  /**
   * Visualize the reading as a card, typically on multiple lines.
   */
  CARD,

  /**
   * Visualize the reading as a row in a table.
   */
  TABLE,
}

export const enum ReadingSeriesVisualization {
  /**
   * Render a reading series as a graph.
   */
  GRAPH,

  /**
   * Render a reading series as a sparkline
   */
  SPARKLINE,

  /**
   * Render a reading series as a table
   */
  TABLE,
}

export interface RendererRegistry {
  /**
   * Registers a renderer for a sensor type selectgor.
   * @param renderer      The renderer to register.
   * @param visualization The visualization provided by the renderer.
   * @param identifier    The identifier of the specific sensor type or class of sensor types to register.
   * @param scope         The scope of the registration
   */
  registerReadingRenderer(
    renderer: SensorReadingRenderer,
    visualization: ReadingVisualization,
    identifier: string,
    scope?: RegistrationScope /*= RegistrationScope.NAME */
  ): void;

  // todo - registration for a reading series renderer.
}

export interface ReadingRendererLookup {
  /**
   * Retrieves a visualization for a SensorType.
   * @param sensorType
   * @param visualization
   */
  findRenderer(
    sensorType: SensorType,
    visualization: ReadingVisualization
  ): SensorReadingRenderer | null;
}

// todo - it would be good to also be able to render nodes, gateways and other items, not just sensor readings.
// That way the default renderers for gateway and nodes can also be plugged in.

type RegistrationKey = {
  /**
   * The level of specifity this registration applies to - a specific sensor type or a class of sensor types.
   */
  scope: RegistrationScope;

  /**
   * The name or measurement to associate this renderer with.
   */
  identifier: string;

  /**
   * The type of visualization provided by the renderer.
   */
  visualization: ReadingVisualization;
};

function identifierFromSensorType(
  registrationScope: RegistrationScope,
  sensorType: SensorType
): string {
  switch (registrationScope) {
    case RegistrationScope.NAME:
      return sensorType.name;
    case RegistrationScope.MEASURE:
      return sensorType.measure;
  }
}

/**
 * Allows more specific registrations to override more general registrations.
 */
export class OverridingRendererRegistry
  implements ReadingRendererLookup, RendererRegistry
{
  constructor(
    private readonly renderers = new Map<string, SensorReadingRenderer>()
  ) {}

  registerReadingRenderer(
    renderer: SensorReadingRenderer,
    visualization: ReadingVisualization,
    identifier: string,
    scope: RegistrationScope = RegistrationScope.NAME
  ): void {
    const registrationKey: RegistrationKey = {
      scope,
      identifier,
      visualization,
    };
    this.renderers.set(this.keyToString(registrationKey), renderer);
  }

  findRenderer(
    sensorType: SensorType,
    visualization: ReadingVisualization
  ): SensorReadingRenderer | null {
    // try from most specific to least specific
    return (
      this.lookup(RegistrationScope.NAME, sensorType, visualization) ||
      this.lookup(RegistrationScope.MEASURE, sensorType, visualization) ||
      null
    );
  }

  private lookup(
    scope: RegistrationScope,
    sensorType: SensorType,
    visualization: ReadingVisualization
  ): SensorReadingRenderer | undefined {
    const identifier = identifierFromSensorType(scope, sensorType);
    return this.renderers.get(
      this.keyToString({
        scope,
        identifier,
        visualization,
      })
    );
  }

  // have to convert the composite key to a string because Maps do not do "by value" quality for non-primitive types.
  // Object equality means reference equality.
  private keyToString(key: RegistrationKey) {
    return `${key.scope}:${key.identifier}:${key.visualization}`;
  }
}
