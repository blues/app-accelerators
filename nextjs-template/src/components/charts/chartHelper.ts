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
};

export function getTooltipDisplayText(label: string, value: number) {
  return `${label}: ${value}`;
}
