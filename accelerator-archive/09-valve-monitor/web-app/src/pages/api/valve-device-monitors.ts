// Next.js API route support: https://nextjs.org/docs/api-routes/introduction
import type { NextApiRequest, NextApiResponse } from "next";
import { ReasonPhrases, StatusCodes } from "http-status-codes";
import { ErrorWithCause } from "pony-cause";
import { serverLogError } from "./log";
import { services } from "../../services/ServiceLocatorServer";

function validateMethod(req: NextApiRequest, res: NextApiResponse) {
  if (req.method !== "GET") {
    res.setHeader("Allow", ["GET"]);
    res.status(StatusCodes.METHOD_NOT_ALLOWED);
    res.json({ err: `Method ${req.method || "is undefined."} Not Allowed` });
    return false;
  }
  return true;
}

async function performRequest() {
  const app = services().getAppService();
  try {
    return await app.getValveMonitorDeviceData();
  } catch (cause) {
    throw new ErrorWithCause("Could not perform request", { cause });
  }
}

export default async function valveMonitorDevicesHandler(
  req: NextApiRequest,
  res: NextApiResponse
) {
  if (!validateMethod(req, res)) {
    return;
  }

  try {
    const valveMonitorDevices = await performRequest();
    res.status(StatusCodes.OK).json({ valveMonitorDevices });
  } catch (cause) {
    res.status(StatusCodes.INTERNAL_SERVER_ERROR);
    res.json({ err: ReasonPhrases.INTERNAL_SERVER_ERROR });
    const e = new ErrorWithCause(
      "Could not fetch valve monitor device data: ",
      {
        cause,
      }
    );
    serverLogError(e);
    throw e;
  }
}
