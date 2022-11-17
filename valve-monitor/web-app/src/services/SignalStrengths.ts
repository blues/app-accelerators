// Wi-Fi, Cellular, or LoRa
export type SignalStrengths = "N/A" | "0" | "1" | "2" | "3" | "4";

export function barsToSignalStrengths(bars: number): SignalStrengths {
  if (bars < 0.1) {
    return "0";
  } else if (bars < 1.5) {
    return "1";
  } else if (bars < 2.5) {
    return "2";
  } else if (bars < 3.5) {
    return "3";
  } else return "4";
}
