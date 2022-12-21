// Wi-Fi, Cellular, or LoRa
export type SignalStrengths = "N/A" | "0" | "1" | "2" | "3" | "4";

export interface LoRaSignalMetrics {
  rssi: number;
  snr?: number;
}

export function LoraSignalMetricsToSignalStrengths(
  metrics?: LoRaSignalMetrics
): SignalStrengths {
  if (!metrics) {
    return "N/A";
  }

  const bars = loraSignalMetricsToBars(metrics);
  return barsToSignalStrengths(bars);
}

function loraSignalMetricsToBars(metrics: LoRaSignalMetrics): number {
  const bars = rssiToBars(metrics.rssi);
  const snrOffset = metrics.snr !== undefined ? snrBarsOffset(metrics.snr) : 0;
  return bars + snrOffset;
}

function snrBarsOffset(snr: number): number {
  if (snr < -10) {
    return -0.5;
  } else if (snr < 0) {
    return -0.25;
  }
  return 0;
}

function rssiToBars(rssi: number): number {
  return (rssi + 120) / 30;
}

function barsToSignalStrengths(bars: number): SignalStrengths {
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

const DEFAULT = { LoraSignalMetricsToSignalStrengths };
export default DEFAULT;
