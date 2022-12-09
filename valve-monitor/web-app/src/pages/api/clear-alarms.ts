// Next.js API route support: https://nextjs.org/docs/api-routes/introduction
import type { NextApiRequest, NextApiResponse } from "next";
import { ErrorWithCause } from "pony-cause";
import { HTTP_STATUS } from "../../constants/http";
import { services } from "../../services/ServiceLocatorServer";

export default async function clearAlarmsHandler(
  req: NextApiRequest,
  res: NextApiResponse
) {
  switch (req.method) {
    case "POST":
      try {
        await services().getAppService().clearAlarms();
      } catch (cause) {
        throw new ErrorWithCause("Could not clear alarms", {
          cause,
        });
      }

      // res.status(StatusCodes.OK).json({});
      res.status(500).json({});

      break;
    default:
      // Other methods not allowed at this route
      res.status(405).json({ err: HTTP_STATUS.METHOD_NOT_ALLOWED });
  }
}
