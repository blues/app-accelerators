// Next.js API route support: https://nextjs.org/docs/api-routes/introduction
import type { NextApiRequest, NextApiResponse } from "next";
import { StatusCodes } from "http-status-codes";
import { ErrorWithCause } from "pony-cause";
import { HTTP_STATUS } from "../../../../constants/http";
import { services } from "../../../../services/ServiceLocatorServer";

interface ValidRequest {
  deviceUID: string;
  flowRateMonitorConfig?: object;
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
  const { deviceUID } = req.query;
  // eslint-disable-next-line @typescript-eslint/no-unsafe-assignment
  const { flowRateMonitorConfig, name } = req.body as ValidRequest;

  if (typeof deviceUID !== "string") {
    res.status(StatusCodes.BAD_REQUEST);
    res.json({ err: HTTP_STATUS.INVALID_DEVICE });
    return false;
  }

  if (typeof name !== "string" && typeof name !== "undefined") {
    res.status(StatusCodes.BAD_REQUEST);
    res.json({ err: HTTP_STATUS.INVALID_DEVICE_NAME });
    return false;
  }

  if (
    typeof flowRateMonitorConfig !== "object" &&
    typeof flowRateMonitorConfig !== "undefined"
  ) {
    res.status(StatusCodes.BAD_REQUEST);
    res.json({ err: HTTP_STATUS.INVALID_FLOW_RATE_MONITOR_CONFIG });
    return false;
  }

  return { deviceUID, flowRateMonitorConfig, name };
}

async function performPostRequest({
  deviceUID,
  flowRateMonitorConfig,
  name,
}: ValidRequest) {
  const app = services().getAppService();

  try {
    if (flowRateMonitorConfig) {
      await app.setDeviceFlowRateMonitorConfig(
        deviceUID,
        flowRateMonitorConfig
      );
    }
    if (name) {
      await app.setDeviceName(deviceUID, name);
    }
  } catch (cause) {
    throw new ErrorWithCause(
      "Could not perform device flow rate monitor configuration",
      {
        cause,
      }
    );
  }
}

export default async function flowRateMonitorConfigHandler(
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
