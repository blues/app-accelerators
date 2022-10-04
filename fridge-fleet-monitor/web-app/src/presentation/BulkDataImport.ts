import { BulkDataImportStatus } from "../services/AppModel";
import { services } from "../services/ServiceLocatorServer";

export type BulkDataImportViewModel = {
  readingCount: string;
};

export type BulkDataImportResultsViewModel = {
  isGood: boolean;
  message: string;
};

export async function buildBulkDataImportViewModel(): Promise<BulkDataImportViewModel> {
  const serviceLocator = services();
  const app = serviceLocator.getAppService();

  const readingCount = `${await app.getReadingCount()}`;

  return { readingCount };
}

export function buildBulkDataImportResultsViewModel({
  elapsedTimeMs,
  err,
  erroredItemCount,
  importedItemCount,
  state,
}: BulkDataImportStatus): BulkDataImportResultsViewModel {
  let isGood: boolean;
  let message: string;

  if (state !== "failed") {
    isGood = true;
    const minutes = Math.round(elapsedTimeMs / 1000 / 60);
    message = `Import ${state}. Imported ${importedItemCount} items in ${minutes} minutes.`;
  } else {
    isGood = false;
    message = `Import Failed: ${err || ""}`;
  }

  if (erroredItemCount > 0)
    message =
      `${message} WARNING: Failed to import ${erroredItemCount} items.` +
      ` See server log for detals`;

  return { isGood, message };
}
