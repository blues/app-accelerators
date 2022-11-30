// Next.js API route support: https://nextjs.org/docs/api-routes/introduction
import type { NextApiRequest, NextApiResponse } from "next";
import { HTTP_STATUS } from "../../../constants/http";
import NotehubRoutedEvent from "../../../services/notehub/models/NotehubRoutedEvent";
import { appEventFromNotehubRoutedEvent } from "../../../services/notehub/AppEvents";
import { services } from "../../../services/ServiceLocatorServer";
import { serverLogInfo } from "../log";

async function ingestEvent(notehubEvent: NotehubRoutedEvent) {
  serverLogInfo("ingesting ", JSON.stringify(notehubEvent));
  if (!notehubEvent.app) {
    throw Error(HTTP_STATUS.INVALID_PROJECTUID); // todo - this is a client error.
  }

  const appEvent = await appEventFromNotehubRoutedEvent(notehubEvent);
  return services().getAppService().handleEvent(appEvent);
}

async function handleEvent(req: NextApiRequest, res: NextApiResponse) {
  const result = await ingestEvent(req.body as NotehubRoutedEvent);
  res.status(200).json({});
  return result;
}

export default async function datastoreIngestionHandler(
  req: NextApiRequest,
  res: NextApiResponse
) {
  switch (req.method) {
    case "POST":
      await handleEvent(req, res);
      break;
    default:
      // Other methods not allowed at this route
      res.status(405).json({ err: HTTP_STATUS.METHOD_NOT_ALLOWED });
  }
}
