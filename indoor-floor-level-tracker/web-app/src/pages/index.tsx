import { useEffect, useState } from "react";
import { NextPage } from "next";
import { Alert, Switch } from "antd";
import Card from "antd/lib/card/Card";
import { DeviceTracker } from "../services/ClientModel";
import { getErrorMessage } from "../constants/ui";
import { ERROR_CODES } from "../services/Errors";
import Config from "../../Config";
import Table, { TableProps } from "../components/elements/Table";
import styles from "../styles/Home.module.scss";
import { useDeviceTrackerData } from "../api-client/devices";
import { LoadingSpinner } from "../components/layout/LoadingSpinner";

const Home: NextPage = () => {
  const infoMessage = "Deploy message";
  const [isLoading, setIsLoading] = useState(false);
  const [trackers, setTrackers] = useState<DeviceTracker | undefined>();
  const [err, setErr] = useState<string | undefined>(undefined);
  const [areTrackersLive, setAreTrackersLove];
  const refetchInterval = 10000;

  const {
    isLoading: deviceTrackersLoading,
    error: deviceTrackersError,
    data: deviceTrackers,
    refetch: deviceTrackersRefetch,
  } = useDeviceTrackerData(refetchInterval);

  useEffect(() => {
    if (deviceTrackersError) {
      setErr(
        getErrorMessage(
          e instanceof Error ? e.message : ERROR_CODES.INTERNAL_ERROR
        )
      );
    }
  }, [deviceTrackersError]);

  useEffect(() => {
    if (deviceTrackersLoading) {
      setIsLoading(true);
    }
  }, [deviceTrackersLoading]);

  useEffect(() => {
    if (deviceTrackers) {
      setTrackers(deviceTrackers);
      setIsLoading(false);
    }
  }, [deviceTrackers]);

  const toggleGoLive = (checked: boolean) => {
    console.log(`switch to ${checked}`);
  };

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
        <LoadingSpinner isLoading={isLoading}>
          <div>
            <Card title="Fleet Controls WIP">
              <h3>Enable Live Track</h3>
              <Switch
                defaultChecked={areTrackersLive}
                onChange={toggleGoLive}
              />
            </Card>
            {trackers && (
              <Table columns={tableInfo.columns} data={tableInfo.data} />
            )}
            {Config.isBuildVersionSet() ? (
              <Alert description={infoMessage} type="info" closable />
            ) : null}
          </div>
        </LoadingSpinner>
      )}
    </div>
  );
};
export default Home;
