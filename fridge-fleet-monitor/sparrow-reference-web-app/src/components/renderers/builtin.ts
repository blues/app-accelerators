/**
 * This file registers the built-in renderers.
 */

import * as Renderers from "./registry";
import * as AppModel from "./../../services/AppModel";
import TextReadingRendererComponent from "../elements/TextReadingRenderer";
import NoOpReadingRendererComponent from "../elements/NoopReadingRenderer";

export function registerRenderers(
  rendererRegistry: Renderers.RendererRegistry
) {
  const defaultRenderer = TextReadingRendererComponent;

  const textRendererTypes = [
    AppModel.GatewaySensorTypeNames.VOLTAGE,
    AppModel.GatewaySensorTypeNames.TEMPERATURE,

    AppModel.NodeSensorTypeNames.TEMPERATURE,
    AppModel.NodeSensorTypeNames.HUMIDITY,
    AppModel.NodeSensorTypeNames.AIR_PRESSURE,
    AppModel.NodeSensorTypeNames.PIR_MOTION,
    AppModel.NodeSensorTypeNames.VOLTAGE,
  ];

  textRendererTypes.forEach((name) => {
    console.log("registering renderer for ", name);
    rendererRegistry.registerReadingRenderer(
      defaultRenderer,
      Renderers.ReadingVisualization.CARD,
      name
    );
  });

  rendererRegistry.registerReadingRenderer(
    NoOpReadingRendererComponent,
    Renderers.ReadingVisualization.CARD,
    AppModel.NodeSensorTypeNames.PIR_MOTION_TOTAL
  );
}
