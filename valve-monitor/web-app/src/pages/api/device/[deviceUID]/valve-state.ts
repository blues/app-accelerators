// Next.js API route support: https://nextjs.org/docs/api-routes/introduction
import type { NextApiRequest, NextApiResponse } from "next";
import { StatusCodes } from "http-status-codes";
import { ErrorWithCause } from "pony-cause";
import { HTTP_STATUS } from "../../../../constants/http";
import { services } from "../../../../services/ServiceLocatorServer";

interface ValidRequest {
  deviceUID: string;
  state: string;
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
  const { deviceUID } = req.query;
  // eslint-disable-next-line @typescript-eslint/no-unsafe-assignment
  const { state } = req.body as ValidRequest;

  if (typeof deviceUID !== "string") {
    res.status(StatusCodes.BAD_REQUEST);
    res.json({ err: HTTP_STATUS.INVALID_DEVICE });
    return false;
  }

  if (typeof state !== "string" && typeof state !== "undefined") {
    res.status(StatusCodes.BAD_REQUEST);
    res.json({ err: HTTP_STATUS.INVALID_VALVE_STATE });
    return false;
  }

  return { deviceUID, state };
}

async function performPostRequest({ deviceUID, state }: ValidRequest) {
  const app = services().getAppService();

  try {
    await app.updateValveState(deviceUID, state);
  } catch (cause) {
    throw new ErrorWithCause("Could not perform change valve state", {
      cause,
    });
  }
}

export default async function valveStateHandler(
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
