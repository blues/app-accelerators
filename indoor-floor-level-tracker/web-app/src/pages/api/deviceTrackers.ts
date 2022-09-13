// Next.js API route support: https://nextjs.org/docs/api-routes/introduction
import type { NextApiRequest, NextApiResponse } from "next";
import { HTTP_STATUS } from "../../constants/http";
import { services } from "../../services/ServiceLocatorServer";

export default async function deviceTrackersHandler(
  req: NextApiRequest,
  res: NextApiResponse
) {
  try {
    switch (req.method) {
      case "GET":
        {
          const deviceTrackers = await services()
            .getAppService()
            .getDeviceTrackerData();
          res.status(200).json({ deviceTrackers });
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
