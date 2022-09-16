import { NextPage } from "next";
import { Alert } from "antd";
import { DeviceTracker } from "../services/ClientModel";
import { getErrorMessage } from "../constants/ui";
import Config from "../../Config";
import Table, { TableProps } from "../components/elements/Table";
import { useDeviceTrackerData } from "../hooks/useDeviceTrackerData";
import { LoadingSpinner } from "../components/layout/LoadingSpinner";
import styles from "../styles/Home.module.scss";

const Home: NextPage = () => {
  const infoMessage = "Deploy message";
  const MS_REFETCH_INTERVAL = 10000;

  const {
    isLoading: deviceTrackersLoading,
    error: deviceTrackersError,
    data: deviceTrackers,
  } = useDeviceTrackerData(MS_REFETCH_INTERVAL);

  const err =
    deviceTrackersError && getErrorMessage(deviceTrackersError.message);

  const trackers: DeviceTracker[] | undefined = deviceTrackers;

  const tableInfo: TableProps = {
    columns: [
      {
        title: "Responders",
        dataIndex: "name",
        key: "name",
      },
      {
        title: "Floor",
        dataIndex: "floor",
        key: "floor",
      },
      {
        title: "Alerts",
        dataIndex: "alerts",
        key: "alerts",
      },
      {
        title: "Last Seen",
        dataIndex: "lastActivity",
        key: "lastActivity",
      },
      {
        title: "Pressure",
        dataIndex: "pressure",
        key: "pressure",
      },
      {
        title: "Voltage",
        dataIndex: "voltage",
        key: "voltage",
      },
    ],
    data: trackers,
  };

  return (
    <div className={styles.container}>
      {err ? (
        <h2
          className={styles.errorMessage}
          // life in the fast lane...
          // eslint-disable-next-line react/no-danger
          dangerouslySetInnerHTML={{ __html: err }}
        />
      ) : (
        <LoadingSpinner isLoading={deviceTrackersLoading}>
          <h3 className={styles.sectionTitle}>Fleet Name</h3>
          {trackers && (
            <Table columns={tableInfo.columns} data={tableInfo.data} />
          )}
          {Config.isBuildVersionSet() ? (
            <Alert description={infoMessage} type="info" closable />
          ) : null}
        </LoadingSpinner>
      )}
    </div>
  );
};
export default Home;
