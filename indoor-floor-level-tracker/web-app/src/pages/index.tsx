import { NextPage } from "next";
import { Alert } from "antd";
import { DeviceTracker } from "../services/ClientModel";
import { getErrorMessage } from "../constants/ui";
import Config from "../../Config";
import Table, { TableProps } from "../components/elements/Table";
import styles from "../styles/Home.module.scss";
import { useDeviceTrackerData } from "../hooks/useDeviceTrackerData";
import { LoadingSpinner } from "../components/layout/LoadingSpinner";

const Home: NextPage = () => {
  const infoMessage = "Deploy message";
  const msRefetchInterval = 10000;

  const {
    isLoading: deviceTrackersLoading,
    error: deviceTrackersError,
    data: deviceTrackers,
  } = useDeviceTrackerData(msRefetchInterval);

  const err =
    deviceTrackersError && getErrorMessage(deviceTrackersError.message);

  const trackers: DeviceTracker[] | undefined = deviceTrackers;

  const tableInfo: TableProps = {
    columns: [
      {
        title: "Device Name",
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
        dataIndex: "alarm",
        key: "alarm",
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
      {
        title: "Last Seen",
        dataIndex: "lastActivity",
        key: "lastActivity",
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
