/* eslint-disable @typescript-eslint/no-unsafe-return */
/* eslint-disable @typescript-eslint/no-unsafe-assignment */
import { formatDistanceToNow, sub } from "date-fns";
import { sortBy, uniqBy } from "lodash";
import { Gateway, SensorTypeCurrentReading } from "../../services/AppModel";
import Reading from "../../services/alpha-models/readings/Reading";
import ReadingSchema from "../../services/alpha-models/readings/ReadingSchema";
import { SignalStrengths } from "../../services/alpha-models/SignalStrengths";
import WifiOff from "../elements/signal-strength-images/wi-fi/wifi-off.svg";
import WifiOne from "../elements/signal-strength-images/wi-fi/wifi-one-bar.svg";
import WifiTwo from "../elements/signal-strength-images/wi-fi/wifi-two-bars.svg";
import WifiThree from "../elements/signal-strength-images/wi-fi/wifi-three-bars.svg";
import WifiFull from "../elements/signal-strength-images/wi-fi/wifi-full.svg";
import CellOff from "../elements/signal-strength-images/cell/cell-off.svg";
import CellOne from "../elements/signal-strength-images/cell/cell-one-bar.svg";
import CellTwo from "../elements/signal-strength-images/cell/cell-two-bars.svg";
import CellThree from "../elements/signal-strength-images/cell/cell-three-bars.svg";
import CellFull from "../elements/signal-strength-images/cell/cell-full.svg";
import LoraOff from "../elements/signal-strength-images/lora/lora-off_v1.svg";
import LoraOne from "../elements/signal-strength-images/lora/lora-one-bar_v1.svg";
import LoraTwo from "../elements/signal-strength-images/lora/lora-two-bar_v1.svg";
import LoraFull from "../elements/signal-strength-images/lora/lora-full_v1.svg";
import { SIGNAL_STRENGTH_TOOLTIP } from "../../constants/ui";

export const getFormattedLastSeenDate = (date: Date) => {
  try {
    return formatDistanceToNow(date, {
      addSuffix: true,
    });
  } catch (e) {
    console.error(e, date);
    return "N/A";
  }
};
// eslint-disable-next-line import/prefer-default-export
export const getFormattedLastSeen = (date: string) =>
  date ? getFormattedLastSeenDate(new Date(date)) : "N/A";

export const getFormattedChartData = (
  readings: Reading<unknown>[],
  readingSchema: ReadingSchema<unknown>
) => {
  if (readings.length) {
    const formattedData = sortBy(
      uniqBy(
        readings
          .filter((reading) => reading.schema === readingSchema)
          .map((filteredEvent) => {
            const chartDataObj = {
              when: filteredEvent.captured,
              value: Number(filteredEvent.value),
            };
            return chartDataObj;
          }),
        "when"
      ),
      "when"
    );
    return formattedData;
  }
  return [];
};

export const getFormattedDoorStatusChartData = (
  readings: Reading<unknown>[],
  readingSchema: ReadingSchema<unknown>
) => {
  if (readings.length) {
    const formattedData = sortBy(
      uniqBy(
        readings
          .filter((reading) => reading.schema === readingSchema)
          .filter((filteredEvents) => filteredEvents.value === "OPEN")
          .map((filteredEvent) => {
            const chartDataObj = {
              when: filteredEvent.captured,
              value: 1,
            };
            return chartDataObj;
          }),
        "when"
      ),
      "when"
    );
    return formattedData;
  }
  return [];
};

export const getFormattedTemperatureData = (
  temperature: number | undefined
) => {
  if (temperature) {
    const formattedData = `${temperature.toFixed(2)}°C`;
    return formattedData;
  }
  return null;
};

export const getFormattedHumidityData = (humidity: number | undefined) => {
  if (humidity) {
    const formattedData = `${humidity.toFixed(2)}%`;
    return formattedData;
  }
  return null;
};

export const getFormattedPressureData = (pressure: number | undefined) => {
  if (pressure) {
    const formattedData = `${pressure.toFixed(2)} kPa`;
    return formattedData;
  }
  return null;
};

export const getFormattedVoltageData = (voltage: number | undefined) => {
  if (voltage !== undefined) {
    const formattedData = `${voltage.toFixed(2)}V`;
    return formattedData;
  }
  return null;
};

export const getFormattedDoorStatusData = (
  doorStatusValue: number | undefined
) => {
  if (doorStatusValue !== undefined && doorStatusValue === 1) {
    const formattedData = "OPEN";
    return formattedData;
  }
  return null;
};

export const calculateLoraSignalStrength = (signalBars: SignalStrengths) => {
  const signalLookup = {
    "N/A": null,
    "0": LoraOff,
    "1": LoraOne,
    "2": LoraTwo,
    "3": LoraFull,
    "4": LoraFull,
  };
  return signalLookup[signalBars];
};

export const calculateWifiSignalStrength = (signalBars: SignalStrengths) => {
  const signalLookup = {
    "N/A": null,
    "0": WifiOff,
    "1": WifiOne,
    "2": WifiTwo,
    "3": WifiThree,
    "4": WifiFull,
  };
  return signalLookup[signalBars];
};

export const calculateCellSignalStrength = (signalBars: SignalStrengths) => {
  const signalLookup = {
    "N/A": null,
    "0": CellOff,
    "1": CellOne,
    "2": CellTwo,
    "3": CellThree,
    "4": CellFull,
  };
  return signalLookup[signalBars];
};

export const calculateSignalTooltip = (signalBars: SignalStrengths) => {
  const tooltipStrength = {
    "N/A": null,
    "0": SIGNAL_STRENGTH_TOOLTIP.OFF,
    "1": SIGNAL_STRENGTH_TOOLTIP.WEAK,
    "2": SIGNAL_STRENGTH_TOOLTIP.FAIR,
    "3": SIGNAL_STRENGTH_TOOLTIP.GOOD,
    "4": SIGNAL_STRENGTH_TOOLTIP.EXCELLENT,
  };
  return tooltipStrength[signalBars];
};

export const getEpochChartDataDate = (minutesToConvert: number) => {
  const date = new Date();
  const rawEpochDate = sub(date, { minutes: minutesToConvert });
  const formattedEpochDate = Math.round(
    rawEpochDate.getTime() / 1000
  ).toString();
  return formattedEpochDate;
};

export function findCurrentReadingWithName(
  gateway: Gateway,
  name: SensorTypeCurrentReading["sensorType"]["name"]
): SensorTypeCurrentReading | undefined {
  return gateway.currentReadings?.find(
    (sensorTypeReading) => name === sensorTypeReading.sensorType.name
  );
}

export function asNumber(value: unknown): number | undefined {
  return typeof value === "number" ? Number(value) : undefined;
}
