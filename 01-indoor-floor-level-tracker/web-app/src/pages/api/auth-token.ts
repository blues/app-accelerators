// Next.js API route support: https://nextjs.org/docs/api-routes/introduction
import type { NextApiRequest, NextApiResponse } from "next";
import { ReasonPhrases, StatusCodes } from "http-status-codes";
import { ErrorWithCause } from "pony-cause";
import { CookieValueTypes } from "cookies-next";
import { serverLogError } from "./log";
import { services } from "../../services/ServiceLocatorServer";
import {
  fetchCookieAuthToken,
  setCookieAuthToken,
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

export function doesAuthTokenExist(authToken: CookieValueTypes) {
  if (authToken === undefined) {
    return false;
  }
  return true;
}

export function isAuthTokenStillValid(authToken: CookieValueTypes) {
  if (typeof authToken === "string") {
    const appService = services().getAppService();
    try {
      const isAuthTokenValid = appService.checkAuthTokenValidity(authToken);
      return isAuthTokenValid;
    } catch (cause) {
      throw new ErrorWithCause("Could not verify auth token validity ", {
        cause,
      });
    }
  }
  return false;
}

async function performRequest() {
  const appService = services().getAppService();

  try {
    return await appService.getAuthToken();
  } catch (cause) {
    throw new ErrorWithCause("Could not perform request", { cause });
  }
}

export default async function authTokenHandler(
  req: NextApiRequest,
  res: NextApiResponse
) {
  const authStringObj = fetchCookieAuthToken(req, res);

  if (!validateMethod(req, res)) {
    return;
  }

  // check if auth token exists, if it does, don't call the api
  const authTokenExists = doesAuthTokenExist(authStringObj);
  if (!authTokenExists) {
    return;
  }

  // check if auth token is still valid, if it is, don't call the api
  const authTokenStillValid = isAuthTokenStillValid(authStringObj);
  if (!authTokenStillValid) {
    return;
  }

  try {
    const authToken = await performRequest();
    setCookieAuthToken(authToken, req, res);
    res.status(StatusCodes.OK).json({});
  } catch (cause) {
    res.status(StatusCodes.INTERNAL_SERVER_ERROR);
    res.json({ err: ReasonPhrases.INTERNAL_SERVER_ERROR });
    const e = new ErrorWithCause("Could not fetch auth token: ", {
      cause,
    });
    serverLogError(e);
    throw e;
  }
}
