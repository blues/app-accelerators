// Next.js API route support: https://nextjs.org/docs/api-routes/introduction
import type { NextApiRequest, NextApiResponse } from "next";
import { StatusCodes } from "http-status-codes";
import { ErrorWithCause } from "pony-cause";
import { HTTP_STATUS } from "../../../constants/http";
import { services } from "../../../services/ServiceLocatorServer";

interface ValidRequest {
  valveMonitorConfig: object;
}

function validateMethod(req: NextApiRequest, res: NextApiResponse) {
  if (req.method !== "POST") {
    res.setHeader("Allow", ["POST"]);
    res.status(StatusCodes.METHOD_NOT_ALLOWED);
    res.json({ err: `Method ${req.method || "is undefined."} Not Allowed` });
    return false;
  }
  return true;
}

function validateRequest(
  req: NextApiRequest,
  res: NextApiResponse
): false | ValidRequest {
  // eslint-disable-next-line @typescript-eslint/no-unsafe-assignment
  const valveMonitorConfig = req.body;

  if (typeof valveMonitorConfig !== "object") {
    res.status(StatusCodes.BAD_REQUEST);
    res.json({ err: HTTP_STATUS.INVALID_VALVE_MONITOR_CONFIG });
    return false;
  }

  return { valveMonitorConfig };
}

async function performPostRequest({ valveMonitorConfig }: ValidRequest) {
  const app = services().getAppService();

  try {
    await app.setValveMonitorConfig(valveMonitorConfig);
  } catch (cause) {
    throw new ErrorWithCause(
      "Could not access fleet valve monitor configuration",
      {
        cause,
      }
    );
  }
}

export default async function valveMonitorConfigHandler(
  req: NextApiRequest,
  res: NextApiResponse
) {
  if (!validateMethod(req, res)) {
    return;
  }

  switch (req.method) {
    case "POST":
      {
        const validRequest = validateRequest(req, res);
        if (!validRequest) {
          return;
        }

        await performPostRequest(validRequest);
        res.status(StatusCodes.OK).json({});
      }
      break;
    default:
      // Other methods not allowed at this route
      res.status(405).json({ err: HTTP_STATUS.METHOD_NOT_ALLOWED });
  }
}
