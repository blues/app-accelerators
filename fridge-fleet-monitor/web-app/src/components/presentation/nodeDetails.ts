import {
  GATEWAY_MESSAGE,
  SENSOR_MESSAGE,
  NODE_MESSAGE,
} from "../../constants/ui";
import NodeDetailViewModel from "../../models/NodeDetailViewModel";
import {
  getFormattedChartData,
  getFormattedHumidityData,
  getFormattedLastSeen,
  getFormattedPressureData,
  getFormattedTemperatureData,
  getFormattedVoltageData,
  calculateLoraSignalStrength,
  calculateSignalTooltip,
  getFormattedDoorStatusChartData,
} from "./uiHelpers";
import Node from "../../services/alpha-models/Node";
import Gateway from "../../services/alpha-models/Gateway";
import Reading from "../../services/alpha-models/readings/Reading";
import TemperatureSensorSchema from "../../services/alpha-models/readings/TemperatureSensorSchema";
import HumiditySensorSchema from "../../services/alpha-models/readings/HumiditySensorSchema";
import PressureSensorSchema from "../../services/alpha-models/readings/PressureSensorSchema";
import VoltageSensorSchema from "../../services/alpha-models/readings/VoltageSensorSchema";
import ContactSwitchSensorSchema from "../../services/alpha-models/readings/ContactSwitchSensorSchema";

// eslint-disable-next-line import/prefer-default-export
export function getNodeDetailsPresentation(
  node?: Node,
  gateway?: Gateway,
  readings?: Reading<unknown>[]
): NodeDetailViewModel {
  return {
    gateway: {
      name: gateway?.name || GATEWAY_MESSAGE.NO_NAME,
    },
    node: node
      ? {
          name: node.name || NODE_MESSAGE.NO_NAME,
          lastActivity: node.lastActivity
            ? getFormattedLastSeen(node.lastActivity)
            : NODE_MESSAGE.NEVER_SEEN,
          location: node?.location || NODE_MESSAGE.NO_LOCATION,
          temperature:
            getFormattedTemperatureData(node.temperature) ||
            SENSOR_MESSAGE.NO_TEMPERATURE,
          humidity:
            getFormattedHumidityData(node.humidity) ||
            SENSOR_MESSAGE.NO_HUMIDITY,
          pressure:
            getFormattedPressureData(node.pressure) ||
            SENSOR_MESSAGE.NO_PRESSURE,
          voltage:
            getFormattedVoltageData(node.voltage) || SENSOR_MESSAGE.NO_VOLTAGE,
          doorStatus: node.doorStatus || SENSOR_MESSAGE.NO_DOOR_STATUS,
          bars: node.bars || "0",
          // eslint-disable-next-line @typescript-eslint/no-unsafe-assignment
          barsIconPath: calculateLoraSignalStrength(node.bars || "0"),
          barsTooltip: calculateSignalTooltip(node.bars || "0"),
        }
      : undefined,
    readings: readings
      ? {
          temperature: getFormattedChartData(readings, TemperatureSensorSchema),
          humidity: getFormattedChartData(readings, HumiditySensorSchema),
          pressure: getFormattedChartData(readings, PressureSensorSchema),
          voltage: getFormattedChartData(readings, VoltageSensorSchema),
          doorStatus: getFormattedDoorStatusChartData(
            readings,
            ContactSwitchSensorSchema
          ),
        }
      : undefined,
  };
}
