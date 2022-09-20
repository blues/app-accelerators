import { NextPage } from "next";
import { useRouter } from "next/router";
import { Alert } from "antd";
import { useState } from "react";
import { useQueryClient } from "react-query";
import { changeDeviceName } from "../api-client/devices";
import { DeviceTracker } from "../services/ClientModel";
import { getErrorMessage } from "../constants/ui";
import Config from "../../Config";
import Table from "../components/elements/Table";
import { useDeviceTrackerData } from "../hooks/useDeviceTrackerData";
import { LoadingSpinner } from "../components/layout/LoadingSpinner";
import styles from "../styles/Home.module.scss";

const Home: NextPage = () => {
  const infoMessage = "Deploy message";
  const MS_REFETCH_INTERVAL = 10000;
  const [isLoading, setIsLoading] = useState<boolean>(false);
  const queryClient = useQueryClient();

  const router = useRouter();
  const refreshData = async () => {
    await router.replace(router.asPath);
  };

  const {
    isLoading: deviceTrackersLoading,
    error: deviceTrackersError,
    data: deviceTrackers,
  } = useDeviceTrackerData(MS_REFETCH_INTERVAL);

  const err =
    deviceTrackersError && getErrorMessage(deviceTrackersError.message);

  const trackers: DeviceTracker[] | undefined = deviceTrackers;

  const onTrackerNameChange = async (deviceUID: string, newName: string) => {
    setIsLoading(true);
    let isSuccessful = true;
    try {
      await changeDeviceName(deviceUID, newName);
    } catch (e) {
      isSuccessful = false;
    }
    // Clear the client-side cache so when we refresh the page
    // it refetches data to get the updated name.
    await queryClient.invalidateQueries();

    setIsLoading(false);
    await refreshData();
    return isSuccessful;
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
        <LoadingSpinner isLoading={isLoading || deviceTrackersLoading}>
          <h3 className={styles.sectionTitle}>Fleet Name</h3>
          {trackers && (
            <Table data={trackers} onNameChange={onTrackerNameChange} />
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
