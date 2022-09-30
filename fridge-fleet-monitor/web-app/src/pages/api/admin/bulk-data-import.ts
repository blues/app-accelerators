// Next.js API route support: https://nextjs.org/docs/api-routes/introduction
import type { NextApiRequest, NextApiResponse } from "next";
import { ReasonPhrases, StatusCodes } from "http-status-codes";
import { ErrorWithCause } from "pony-cause";
import { services } from "../../../services/ServiceLocatorServer";
import { BulkDataImportStatus } from "../../../services/AppModel";
import { serverLogError } from "../log";

const IMPORT = "import";

interface ValidRequest {
  action: typeof IMPORT;
}

function validateMethod(req: NextApiRequest, res: NextApiResponse) {
  if (req.method !== "PUT") {
    res.setHeader("Allow", ["PUT"]);
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
  const { action } = req.body;

  // action must be
  if (typeof action !== "string" || action !== IMPORT) {
    res.status(StatusCodes.BAD_REQUEST);
    res.json({ err: `action must be ${IMPORT}` });
    return false;
  }

  return { action };
}

async function performRequest(): Promise<BulkDataImportStatus | undefined> {
  const app = services().getAppService();
  try {
    return await app.performBulkDataImport();
  } catch (cause) {
    throw new ErrorWithCause("Could not perform request", { cause });
  }
}

export default async function bulkDataImportHandler(
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
    const result = await performRequest();
    res.status(StatusCodes.OK).json(result);
  } catch (cause) {
    res.status(StatusCodes.INTERNAL_SERVER_ERROR);
    res.json({ err: ReasonPhrases.INTERNAL_SERVER_ERROR });
    const e = new ErrorWithCause("bulkDataImportHandler Error:", { cause });
    serverLogError(e);
    throw e;
  }
}
