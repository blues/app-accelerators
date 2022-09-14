// Next.js API route support: https://nextjs.org/docs/api-routes/introduction
import type { NextApiRequest, NextApiResponse } from "next";
import { ReasonPhrases, StatusCodes } from "http-status-codes";
import { ErrorWithCause } from "pony-cause";
import { HTTP_STATUS } from "../../../constants/http";
import { services } from "../../../services/ServiceLocatorServer";
import { serverLogError } from "../log";
import { TrackerConfig } from "../../../services/ClientModel";
import Config from "../../../../Config";

interface ValidRequest {
  trackerConfig: string;
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
  const trackerConfig = req.body;

  if (typeof trackerConfig !== "string") {
    res.status(StatusCodes.BAD_REQUEST);
    res.json({ err: HTTP_STATUS.INVALID_TRACKER_CONFIG });
    return false;
  }

  return { trackerConfig };
}

async function performRequest({ trackerConfig }: ValidRequest) {
  const app = services().getAppService();
  const parsedTrackerConfig = JSON.parse(trackerConfig) as TrackerConfig;

  try {
    await app.setTrackerConfig(Config.hubFleetUID, parsedTrackerConfig);
  } catch (cause) {
    throw new ErrorWithCause("Could not perform request", { cause });
  }
}

export default async function trackerConfigHandler(
  req: NextApiRequest,
  res: NextApiResponse
) {
  if (!validateMethod(req, res)) {
    return;
  }
  const validRequest = validateRequest(req, res);
  if (!validRequest) {
    return;
  }

  try {
    await performRequest(validRequest);
    res.status(StatusCodes.OK).json({});
  } catch (cause) {
    res.status(StatusCodes.INTERNAL_SERVER_ERROR);
    res.json({ err: ReasonPhrases.INTERNAL_SERVER_ERROR });
    const e = new ErrorWithCause("Could not update tracker configuration", {
      cause,
    });
    serverLogError(e);
    throw e;
  }
}
