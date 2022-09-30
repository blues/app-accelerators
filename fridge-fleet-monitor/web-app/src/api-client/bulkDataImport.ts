import axios from "axios";
import { BulkDataImportStatus } from "../services/AppModel";
import { services } from "../services/ServiceLocatorClient";

export async function performBulkDataImport() {
  const endpoint = services().getUrlManager().performBulkDataImportApi();
  const postBody = { action: "import" };
  const resp = await axios.put(endpoint, postBody);
  return resp.data as BulkDataImportStatus;
}

const DEFAULT = { performBulkDataImport };
export default DEFAULT;
