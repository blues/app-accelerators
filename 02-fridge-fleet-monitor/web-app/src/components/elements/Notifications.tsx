import { Alert, Button } from "antd";
import { NextRouter, useRouter } from "next/router";
import { presentNotifications as apiAppNotifications, removeNotification } from "../../api-client/notification";
import { services } from "../../services/ServiceLocatorClient";
import {
  AppNotification,
  NodePairedWithGatewayAppNotification,
  NodePairedWithGatewayAppNotificationType,
} from "../presentation/notifications";
import { useQuery } from "react-query";
import React from "react";
import styles from "../../styles/Home.module.scss";
import notificationsStyles from "../../styles/Notifications.module.scss";
import { getFormattedLastSeenDate } from "../presentation/uiHelpers";

async function nodePairedAction(n: NodePairedWithGatewayAppNotification, router: NextRouter) {
  await removeNotification(n.id);
  const url = services()
    .getUrlManager()
    .nodeSettings(n.gateway.uid, n.node.nodeId);
  await router.push(url);
}

function renderNodePairedNotification(n: NodePairedWithGatewayAppNotification, router: NextRouter) {

  const whenPaired = getFormattedLastSeenDate(new Date(n.when));
  const note = <span>Node ending <span title={n.node.nodeId}>{n.node.nodeId.slice(-5)}</span> was <span title={whenPaired}>recently</span> paired {whenPaired} with gateway <span>{n.gateway.name}</span>.</span>;
  //const details = <span>Node ID: <span>{n.node.nodeId}</span></span>
  return (
    <Alert key={n.id}
      banner
      message={note}
      type="info"
      closable
      onClose={async () => await removeNotification(n.id)}
      action={
        <Button type="primary" onClick={async () => await nodePairedAction(n, router)}>
          Node Settings
        </Button>
      }
    />
  );
}

interface NotificationProps {
  items: AppNotification[];
}

function renderNotification(notification: AppNotification, router: NextRouter) {
  if (notification.type === NodePairedWithGatewayAppNotificationType) {
    return renderNodePairedNotification(notification as NodePairedWithGatewayAppNotification, router);
  }
  return null;
}

const NOTIFICATION_REFETCH_INTERVAL = 5000;

const NotificationsComponent = (props: NotificationProps) => {
  const router = useRouter();
  const { data, status } = useQuery("notifications", apiAppNotifications, { refetchInterval: NOTIFICATION_REFETCH_INTERVAL });
  return <div className={notificationsStyles.notifications}> {
      status==="success" &&
      data?.notifications.map((notification) =>
        renderNotification(notification, router)
      ).filter(n => n)
  }</div>;
};

export default NotificationsComponent;