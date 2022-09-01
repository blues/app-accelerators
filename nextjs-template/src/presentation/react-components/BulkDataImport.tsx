import Title from "antd/lib/typography/Title";
import { useCallback, useState } from "react";
import { Alert, Breadcrumb, Button, Space, Statistic } from "antd";
import Paragraph from "antd/lib/typography/Paragraph";
import Link from "next/link";
import {
  buildBulkDataImportResultsViewModel,
  BulkDataImportViewModel,
} from "../BulkDataImport";
import { performBulkDataImport } from "../../api-client/bulkDataImport";

const BulkDataImport = ({ eventCount: readingCount }: BulkDataImportViewModel) => {
  const [isInProgress, setIsInProgress] = useState(false);
  const [successMessage, setSuccessMessage] = useState("");
  const [errorMessage, setErrorMessage] = useState("");

  const performImport = useCallback(async () => {
    setIsInProgress(true);
    setErrorMessage("");
    setSuccessMessage("");

    const resp = await performBulkDataImport();
    const { message, isGood } = buildBulkDataImportResultsViewModel(resp);

    if (isGood) setSuccessMessage(message);
    else setErrorMessage(message);

    setIsInProgress(false);
  }, []);

  return (
    <Space direction="vertical" size="middle">
      <Breadcrumb>
        <Breadcrumb.Item>
          <Link href="/">Home</Link>
        </Breadcrumb.Item>
        <Breadcrumb.Item>
          <Link href="/admin">Admin</Link>
        </Breadcrumb.Item>
        <Breadcrumb.Item>
          <Link href="/admin/bulk-data-import">Bulk Data Import</Link>
        </Breadcrumb.Item>
      </Breadcrumb>
      <Title level={2}>Bulk Data Import</Title>
      <Paragraph>
        <strong>Import reading history from Notehub</strong> into the the application
        database. Import is useful to get data from periods when routing was not
        operational. For example, a period when the Notehub route was not
        configured yet or when there was downtime on this web app or its
        database. Bulk Data Import is limited to the Event Retention period of
        your Notehub subscription or 10 days, whichever is shorter. Import is
        safe to run repeatedly (idempotent).
      </Paragraph>
      <Space direction="horizontal" wrap>
        <Button
          type="primary"
          size="large"
          loading={isInProgress}
          onClick={performImport}
        >
          Import
        </Button>
        {isInProgress && (
          <Alert message="This can take a few minutues." type="info" />
        )}
        {successMessage && <Alert message={successMessage} type="success" />}
        {errorMessage && <Alert message={errorMessage} type="error" />}
      </Space>
      <Statistic
        title="Database Contents (at page load)"
        value={readingCount}
        suffix="Readings"
      />
    </Space>
  );
};

export default BulkDataImport;
