// Next.js API route support: https://nextjs.org/docs/api-routes/introduction
import type { NextApiRequest, NextApiResponse } from "next";
import { StatusCodes } from "http-status-codes";
import { ErrorWithCause } from "pony-cause";
import { CookieValueTypes } from "cookies-next";
import { HTTP_STATUS } from "../../../constants/http";
import { services } from "../../../services/ServiceLocatorServer";
import {
  fetchCookieAuthToken,
  normalizeStringToAuthToken,
} from "../../../authorization/cookieAuth";

interface ValidRequest {
  trackerConfig: object;
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

  if (typeof trackerConfig !== "object") {
    res.status(StatusCodes.BAD_REQUEST);
    res.json({ err: HTTP_STATUS.INVALID_TRACKER_CONFIG });
    return false;
  }

  return { trackerConfig };
}

async function performPostRequest(
  authStringObj: CookieValueTypes,
  { trackerConfig }: ValidRequest
) {
  const appService = services().getAppService();

  try {
    if (typeof authStringObj === "string") {
      const authObj = normalizeStringToAuthToken(authStringObj);
      await appService.setTrackerConfig(authObj, trackerConfig);
    }
  } catch (cause) {
    throw new ErrorWithCause("Could not access tracker configuration", {
      cause,
    });
  }
}

export default async function trackerConfigHandler(
  req: NextApiRequest,
  res: NextApiResponse
) {
  const authStringObj = fetchCookieAuthToken(req, res);

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
        await performPostRequest(authStringObj, validRequest);
        res.status(StatusCodes.OK).json({});
      }
      break;
    default:
      // Other methods not allowed at this route
      res.status(405).json({ err: HTTP_STATUS.METHOD_NOT_ALLOWED });
  }
}
