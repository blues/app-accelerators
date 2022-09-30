import Node from "../services/alpha-models/Node";
import { SignalStrengths } from "../services/alpha-models/SignalStrengths";

interface GatewayDetailViewModel {
  gateway?: {
    uid: string;
    name: string;
    lastActivity: string;
    location: string;
    cellBars?: SignalStrengths;
    cellBarsIconPath?: string | null;
    cellBarsTooltip?: string | null;
    wifiBars?: SignalStrengths;
    wifiBarsIconPath?: string | null;
    wifiBarsTooltip?: string | null;
  };
  nodes?: Node[];
  readOnly?: boolean;
}

export default GatewayDetailViewModel;
