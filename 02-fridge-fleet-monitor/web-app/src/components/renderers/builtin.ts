/**
 * This file registers the built-in renderers.
 */

import * as Renderers from "./registry";
import * as AppModel from "../../services/AppModel";
import TextReadingRendererComponent from "../elements/TextReadingRenderer";

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
    AppModel.NodeSensorTypeNames.DOOR_STATUS,
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
}
