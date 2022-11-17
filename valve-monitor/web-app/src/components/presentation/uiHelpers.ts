/* eslint-disable @typescript-eslint/no-unsafe-return */
/* eslint-disable @typescript-eslint/no-unsafe-assignment */
import { formatDistanceToNow, sub } from "date-fns";
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
import { SIGNAL_STRENGTH_TOOLTIP } from "../../constants/ui";
import { SignalStrengths } from "../../services/SignalStrengths";

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
  getFormattedLastSeenDate(new Date(date));


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


export function asNumber(value: unknown): number | undefined {
  return typeof value === "number" ? Number(value) : undefined;
}
