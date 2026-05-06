// Next.js API route support: https://nextjs.org/docs/api-routes/introduction
import type { NextApiRequest, NextApiResponse } from "next";
import { HTTP_STATUS } from "../../constants/http";
import { services } from "../../services/ServiceLocatorServer";

const APP_FORMAT = "app";

export default async function notificationsHandler(
  req: NextApiRequest,
  res: NextApiResponse
) {
  try {
    switch (req.method) {
      case "GET": {
        const { format } = req.query;  // app notifications
        const notifications = await (format===APP_FORMAT ?
          services().getAppService().getAppNotifications() :
          services().getNotificationsStore().getNotifications());
        res.status(200).json({ notifications });
      }
      break;
      case "DELETE": {
        const id = req.query.id;
        const notificationIDs = typeof id === "string" ? [id] : id;
        await services()
          .getNotificationsStore()
          .removeNotifications(notificationIDs);
        res.status(200).json(notificationIDs);
      }
      break;
      default:
        // Other methods not allowed at this route
        res.status(405).json({ err: HTTP_STATUS.METHOD_NOT_ALLOWED });
    }
  } catch (e: any) {
    if (e.err === HTTP_STATUS.INVALID_REQUEST) {
      res.status(400).json(e);
    }
  }
}
