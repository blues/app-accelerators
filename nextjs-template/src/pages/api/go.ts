// Next.js API route support: https://nextjs.org/docs/api-routes/introduction
import type { NextApiRequest, NextApiResponse } from "next";
import { HTTP_STATUS } from "../../constants/http";
import { services } from "../../services/ServiceLocatorServer";

function isString(x: unknown): x is string {
  return typeof x === "string" && !!x;
}

function asString(x: unknown): string {
  return isString(x) ? x : "";
}

async function handleRedirect(req: NextApiRequest, res: NextApiResponse) {
  const { pin, device } = req.query;
  // query params can be arrays too, though we don't support multiple values here.
  if (!isString(device) || !isString(pin)) {
    res.status(400).json({ err: HTTP_STATUS.INVALID_REQUEST });
    return;
  }

  res.status(404).json({ err: HTTP_STATUS.INVALID_REQUEST });
}

export default async function goHandler(
  req: NextApiRequest,
  res: NextApiResponse
) {
  switch (req.method) {
    case "GET":
      await handleRedirect(req, res);
      break;
    default:
      // Other methods not allowed at this route
      res.status(405).json({ err: HTTP_STATUS.METHOD_NOT_ALLOWED });
  }
}
