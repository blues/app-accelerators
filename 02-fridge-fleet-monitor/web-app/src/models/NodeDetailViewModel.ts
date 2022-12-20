import { SignalStrengths } from "../services/alpha-models/SignalStrengths";

interface NodeDetailViewModel {
  gateway?: {
    name: string;
  };
  node?: {
    name: string;
    location: string;
    lastActivity: string;
    temperature: string;
    humidity: string;
    pressure: string;
    voltage: string;
    doorStatus: string;
    bars: SignalStrengths;
    barsIconPath: string | null;
    barsTooltip: string | null;
  };
  readings?: {
    temperature: { when: string; value: number }[];
    humidity: { when: string; value: number }[];
    pressure: { when: string; value: number }[];
    voltage: { when: string; value: number }[];
    doorStatus: { when: string; value: number }[];
  };
  readOnly?: boolean;
}

export default NodeDetailViewModel;
