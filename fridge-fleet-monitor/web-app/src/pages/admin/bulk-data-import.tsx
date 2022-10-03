/* eslint-disable react/jsx-props-no-spreading */
import { GetServerSideProps, NextPage } from "next";
import {
  buildBulkDataImportViewModel,
  BulkDataImportViewModel,
} from "../../presentation/BulkDataImport";
import BulkDataImport from "../../presentation/react-components/BulkDataImport";

const BulkDataImportPage: NextPage<BulkDataImportViewModel> = (
  viewModel: BulkDataImportViewModel
) => <BulkDataImport {...viewModel} />;

export const getServerSideProps: GetServerSideProps<
  BulkDataImportViewModel
> = async () => {
  const viewModel = await buildBulkDataImportViewModel();
  return Promise.resolve({
    props: viewModel,
  });
};

export default BulkDataImportPage;
