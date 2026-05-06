// Next.js API route support: https://nextjs.org/docs/api-routes/introduction
import type { NextApiRequest, NextApiResponse } from "next";
import { ReasonPhrases, StatusCodes } from "http-status-codes";
import { ErrorWithCause } from "pony-cause";
import { HTTP_STATUS } from "../../../../constants/http";
import { services } from "../../../../services/ServiceLocatorServer";
import { serverLogError } from "../../log";

interface ValidRequest {
  deviceUID: string;
  name: string;
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
  const { name } = req.body;

  // Device UID must be a string
  if (typeof deviceUID !== "string") {
    res.status(StatusCodes.BAD_REQUEST);
    res.json({ err: HTTP_STATUS.INVALID_DEVICE });
    return false;
  }
  if (typeof name !== "string") {
    res.status(StatusCodes.BAD_REQUEST);
    res.json({ err: HTTP_STATUS.INVALID_DEVICE_NAME });
    return false;
  }

  return { deviceUID, name };
}

async function performRequest({ deviceUID, name }: ValidRequest) {
  const app = services().getAppService();
  try {
    await app.setDeviceName(deviceUID, name);
  } catch (cause) {
    throw new ErrorWithCause("Could not perform request", { cause });
  }
}

export default async function deviceNameHandler(
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
    const e = new ErrorWithCause("could not set device name", { cause });
    serverLogError(e);
    throw e;
  }
}
