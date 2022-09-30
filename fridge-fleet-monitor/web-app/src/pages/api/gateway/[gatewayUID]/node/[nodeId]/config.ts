// Next.js API route support: https://nextjs.org/docs/api-routes/introduction
import { ErrorWithCause } from "pony-cause";
import { ReasonPhrases, StatusCodes } from "http-status-codes";
import type { NextApiRequest, NextApiResponse } from "next";

import { services } from "../../../../../../services/ServiceLocatorServer";
import { HTTP_STATUS } from "../../../../../../constants/http";
import { serverLogError } from "../../../../log";

interface ValidRequest {
  gatewayUID: string;
  nodeId: string;
  location?: string;
  name?: string;
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
  const { gatewayUID, nodeId } = req.query;
  const { name, location } = req.body as ValidRequest;

  if (typeof gatewayUID !== "string") {
    res.status(StatusCodes.BAD_REQUEST);
    res.json({ err: HTTP_STATUS.INVALID_GATEWAY });
    return false;
  }
  if (typeof nodeId !== "string") {
    res.status(StatusCodes.BAD_REQUEST);
    res.json({ err: HTTP_STATUS.INVALID_NODE_ID });
    return false;
  }
  if (typeof location !== "string" && typeof location !== "undefined") {
    res.status(StatusCodes.BAD_REQUEST);
    res.json({ err: "location should be a string or undefined" });
    return false;
  }
  if (typeof name !== "string" && typeof name !== "undefined") {
    res.status(StatusCodes.BAD_REQUEST);
    res.json({ err: "name should be a string or undefined" });
    return false;
  }
  if (typeof location === "undefined" && typeof name === "undefined") {
    res.status(StatusCodes.BAD_REQUEST);
    res.json({ err: HTTP_STATUS.INVALID_NODE_CONFIG_BODY });
    return false;
  }

  return { gatewayUID, nodeId, location, name };
}

async function performRequest({
  nodeId,
  gatewayUID,
  location,
  name,
}: ValidRequest) {
  const app = services().getAppService();
  try {
    if (location) await app.setNodeLocation(gatewayUID, nodeId, location);
    if (name) await app.setNodeName(gatewayUID, nodeId, name);
  } catch (cause) {
    throw new ErrorWithCause("Could not perform request", { cause });
  }
}

export default async function nodeConfigHandler(
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
    const e = new ErrorWithCause("could not handle node config", { cause });
    serverLogError(e);
    throw e;
  }
}
