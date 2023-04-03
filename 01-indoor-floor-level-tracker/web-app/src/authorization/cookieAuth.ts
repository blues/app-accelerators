import type { NextApiRequest, NextApiResponse } from "next";
import { getCookie, setCookie } from "cookies-next";
import { IncomingMessage, ServerResponse } from "http";
import { NextApiRequestCookies } from "next/dist/server/api-utils";
import { AuthToken } from "../services/AppModel";

export function fetchCookieAuthToken(
  req: NextApiRequest | (IncomingMessage & { cookies: NextApiRequestCookies }),
  res: NextApiResponse | ServerResponse
) {
  console.log("Fetching auth token string from cookie");
  return getCookie("authTokenObj", { req, res });
}

export function normalizeStringToAuthToken(cookieContents: string) {
  const authTokenObject: AuthToken = JSON.parse(cookieContents);
  return authTokenObject;
}

export function setCookieAuthToken(
  authToken: AuthToken | string,
  req: NextApiRequest | (IncomingMessage & { cookies: NextApiRequestCookies }),
  res: NextApiResponse | ServerResponse
) {
  console.log("Setting auth token string from cookie");
  let authStringObj = authToken;
  if (typeof authToken === "object") {
    authStringObj = JSON.stringify(authToken);
  }
  setCookie("authTokenObj", authStringObj, { req, res });
}
