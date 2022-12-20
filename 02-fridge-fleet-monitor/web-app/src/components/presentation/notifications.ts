import Gateway from "../../services/alpha-models/Gateway";
import Node from "../../services/alpha-models/Node";

export interface AppNotification {
  readonly id: string;
  readonly type: string;
  readonly when: number;
}

export const NodePairedWithGatewayAppNotificationType: string = 'NodePairedWithGatewayAppNotification';
export interface NodePairedWithGatewayAppNotification extends AppNotification {
    readonly gateway: Gateway;
    readonly node: Node;
}
