import {
  Chart as ChartJS,
  BarController,
  BarElement,
  ChartOptions,
  CategoryScale,
  Filler,
  LinearScale,
  PointElement,
  LineElement,
  TimeScale,
  Title,
  Tooltip,
  Legend,
} from "chart.js";
import "chartjs-adapter-date-fns";
import CountSensorSchema from "../../services/alpha-models/readings/CountSensorSchema";
import HumiditySensorSchema from "../../services/alpha-models/readings/HumiditySensorSchema";
import PressureSensorSchema from "../../services/alpha-models/readings/PressureSensorSchema";
import ReadingSchema from "../../services/alpha-models/readings/ReadingSchema";
import TemperatureSensorSchema from "../../services/alpha-models/readings/TemperatureSensorSchema";
import VoltageSensorSchema from "../../services/alpha-models/readings/VoltageSensorSchema";
import {
  getFormattedCountData,
  getFormattedHumidityData,
  getFormattedPressureData,
  getFormattedTemperatureData,
  getFormattedVoltageData,
} from "../presentation/uiHelpers";

ChartJS.register(
  BarController,
  BarElement,
  CategoryScale,
  Filler,
  LinearScale,
  PointElement,
  LineElement,
  TimeScale,
  Title,
  Tooltip,
  Legend
);

// See https://date-fns.org/v2.27.0/docs/format
export const CHART_DATE_FORMAT = "MMM do hh:mm aa";

export const GLOBAL_CHART_OPTIONS: ChartOptions<"line" | "bar"> = {
  responsive: true,
  interaction: {
    mode: "index",
    intersect: false,
  },
};

export function getChartOptions(
  overrides?: ChartOptions<"line" | "bar">
): ChartOptions<"line" | "bar"> {
  return {
    ...GLOBAL_CHART_OPTIONS,
    ...overrides,
  };
}

export type NodeDetailsChartProps = {
  label: string;
  chartColor: string;
  data: {
    when: string;
    value: number;
  }[];
  schema: ReadingSchema<number>;
};

export function getTooltipDisplayText(
  label: string,
  schema: ReadingSchema<number>,
  value: number
) {
  let valueDisplay = "";
  switch (schema) {
    case TemperatureSensorSchema:
      valueDisplay = getFormattedTemperatureData(value) || "";
      break;
    case HumiditySensorSchema:
      valueDisplay = getFormattedHumidityData(value) || "";
      break;
    case VoltageSensorSchema:
      valueDisplay = getFormattedVoltageData(value) || "";
      break;
    case PressureSensorSchema:
      valueDisplay = getFormattedPressureData(value) || "";
      break;
    case CountSensorSchema:
      valueDisplay = getFormattedCountData(value) || "";
      break;
    default:
      // eslint-disable-next-line no-console
      console.error(`Unknown schema ${schema.toString()}`);
  }
  return `${label}: ${valueDisplay}`;
}
