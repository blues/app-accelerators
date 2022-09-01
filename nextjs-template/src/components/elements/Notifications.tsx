import { Alert, Button } from "antd";
import { NextRouter, useRouter } from "next/router";
import { presentNotifications as apiAppNotifications, removeNotification } from "../../api-client/notification";
import { services } from "../../services/ServiceLocatorClient";
import {
  AppNotification,
} from "../presentation/notifications";
import { useQuery } from "react-query";
import React from "react";
import styles from "../../styles/Home.module.scss";
import notificationsStyles from "../../styles/Notifications.module.scss";
import { getFormattedLastSeenDate } from "../presentation/uiHelpers";


function renderAppNotification(n: AppNotification, router: NextRouter) {

  const whenPaired = getFormattedLastSeenDate(new Date(n.when));
  const note = "some message";
  return (
    <Alert key={n.id}
      banner
      message={note}
      type="info"
      closable
      onClose={async () => await removeNotification(n.id)}
    />
  );
}

interface NotificationProps {
  items: AppNotification[];
}

function renderNotification(notification: AppNotification, router: NextRouter) {
  // test the type of notification
  // if (notification.type === MyApplicationNotificationType) {
  //   return renderMyApplicationNotificationType(notification as MyApplicationNotification, router);
  // }
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