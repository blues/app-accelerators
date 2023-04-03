// Next.js API route support: https://nextjs.org/docs/api-routes/introduction
import type { NextApiRequest, NextApiResponse } from "next";
import { ReasonPhrases, StatusCodes } from "http-status-codes";
import { ErrorWithCause } from "pony-cause";
import { CookieValueTypes } from "cookies-next";
import { serverLogError } from "./log";
import { services } from "../../services/ServiceLocatorServer";
import {
  fetchCookieAuthToken,
  normalizeStringToAuthToken,
} from "../../authorization/cookieAuth";

function validateMethod(req: NextApiRequest, res: NextApiResponse) {
  if (req.method !== "GET") {
    res.setHeader("Allow", ["GET"]);
    res.status(StatusCodes.METHOD_NOT_ALLOWED);
    res.json({ err: `Method ${req.method || "is undefined."} Not Allowed` });
    return false;
  }
  return true;
}

async function performRequest(authStringObj: CookieValueTypes) {
  const appService = services().getAppService();

  try {
    if (typeof authStringObj === "string") {
      const authObj = normalizeStringToAuthToken(authStringObj);
      return await appService.getDeviceTrackerData(authObj);
    }
  } catch (cause) {
    throw new ErrorWithCause("Could not perform request", { cause });
  }
}

export default async function deviceTrackersHandler(
  req: NextApiRequest,
  res: NextApiResponse
) {
  const authStringObj = fetchCookieAuthToken(req, res);

  if (!validateMethod(req, res)) {
    return;
  }

  try {
    const deviceTrackers = await performRequest(authStringObj);
    res.status(StatusCodes.OK).json({ deviceTrackers });
  } catch (cause) {
    res.status(StatusCodes.INTERNAL_SERVER_ERROR);
    res.json({ err: ReasonPhrases.INTERNAL_SERVER_ERROR });
    const e = new ErrorWithCause("Could not fetch device tracker data: ", {
      cause,
    });
    serverLogError(e);
    throw e;
  }
}
