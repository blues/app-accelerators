import { GetServerSideProps, NextPage } from "next";
import { useRouter } from "next/router";
import { Alert, Col, Row } from "antd";
import { useEffect, useState } from "react";
import { useQueryClient } from "react-query";
import { getErrorMessage } from "../constants/ui";
import { ERROR_CODES } from "../services/Errors";
import { DeviceTracker, TrackerConfig } from "../services/AppModel";
import Config from "../../Config";
import { useDeviceTrackerData } from "../hooks/useDeviceTrackerData";
import { services } from "../services/ServiceLocatorServer";
import { LoadingSpinner } from "../components/layout/LoadingSpinner";
import LiveTrackCard from "../components/elements/LiveTrackCard";
import RespondersByFloorTable from "../components/elements/RespondersByFloorTable";
import TrackerTable from "../components/elements/TrackerTable";
import MotionAlertConfigCard from "../components/elements/MotionAlertConfigCard";
import styles from "../styles/Home.module.scss";

type HomeData = {
  fleetTrackerConfig: TrackerConfig;
  error: string;
};

const Home: NextPage<HomeData> = ({ fleetTrackerConfig, error }) => {
  const infoMessage = "Deploy message";
  const MS_REFETCH_INTERVAL = 10000;
  const [isLoading, setIsLoading] = useState<boolean>(false);
  const [isErrored, setIsErrored] = useState<boolean>(false);
  const [errorMessage, setErrorMessage] = useState<string>("");
  const [isLiveTrackingEnabled, setIsLiveTrackingEnabled] =
    useState<boolean>(false);
  const [currentNoMovementValue, setCurrentNoMovementValue] =
    useState<number>(120);

  const router = useRouter();
  const queryClient = useQueryClient();
  // refresh the page
  const refreshData = async () => {
    // eslint-disable-next-line no-void
    void router.replace(router.asPath);
  };
  const refreshDataAndInvalidateCache = async () => {
    // Clear the client-side cache so when we refresh the page
    // it refetches data to get the updated name.
    await queryClient.invalidateQueries();
    await refreshData();
  };

  const { error: deviceTrackersError, data: deviceTrackers } =
    useDeviceTrackerData(MS_REFETCH_INTERVAL);

  const err =
    deviceTrackersError && getErrorMessage(deviceTrackersError.message);

  const trackers: DeviceTracker[] | undefined = deviceTrackers;

  useEffect(() => {
    if (fleetTrackerConfig && fleetTrackerConfig.live) {
      setIsLiveTrackingEnabled(fleetTrackerConfig.live);
    }
    if (fleetTrackerConfig && fleetTrackerConfig.noMovementThreshold) {
      setCurrentNoMovementValue(fleetTrackerConfig.noMovementThreshold);
    }
  }, [fleetTrackerConfig]);

  useEffect(() => {
    refreshData();
  }, [isLiveTrackingEnabled, currentNoMovementValue]);

  return (
    <div className={styles.container}>
      {err ? (
        <h2
          className={styles.errorMessage}
          // life in the fast lane...
          // eslint-disable-next-line react/no-danger
          dangerouslySetInnerHTML={{ __html: err || error }}
        />
      ) : (
        <LoadingSpinner isLoading={isLoading}>
          <div>
            {isErrored && (
              <Alert type="error" message={errorMessage} closable />
            )}
            {trackers && (
              <>
                <h3 className={styles.sectionTitle}>Fleet Controls</h3>
                <Row gutter={16}>
                  <Col className={styles.motionAlertCard}>
                    <MotionAlertConfigCard
                      setIsErrored={setIsErrored}
                      setIsLoading={setIsLoading}
                      setErrorMessage={setErrorMessage}
                      currentNoMovementThreshold={currentNoMovementValue}
                      setCurrentNoMovementThreshold={setCurrentNoMovementValue}
                    />
                  </Col>
                  <Col className={styles.liveTrackCard}>
                    <LiveTrackCard
                      setIsErrored={setIsErrored}
                      setIsLoading={setIsLoading}
                      setErrorMessage={setErrorMessage}
                      isLiveTrackingEnabled={isLiveTrackingEnabled}
                      setIsLiveTrackingEnabled={setIsLiveTrackingEnabled}
                    />
                  </Col>
                </Row>
                <h3 className={styles.sectionTitle}>My Fleet</h3>
                <Row gutter={[16, 24]}>
                  <Col xs={24} sm={24} md={24} lg={20}>
                    <TrackerTable
                      setIsErrored={setIsErrored}
                      setIsLoading={setIsLoading}
                      setErrorMessage={setErrorMessage}
                      refreshData={refreshDataAndInvalidateCache}
                      data={trackers}
                    />
                  </Col>
                  <Col xs={12} sm={7} md={6} lg={4}>
                    <RespondersByFloorTable data={trackers} />
                  </Col>
                </Row>
              </>
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

export const getServerSideProps: GetServerSideProps<HomeData> = async () => {
  let fleetTrackerConfig: TrackerConfig = {};
  let error = "";

  try {
    const appService = services().getAppService();
    fleetTrackerConfig = await appService.getTrackerConfig();

    return {
      props: { fleetTrackerConfig, error },
    };
  } catch (e) {
    error = getErrorMessage(
      e instanceof Error ? e.message : ERROR_CODES.INTERNAL_ERROR
    );
  }

  return {
    props: { fleetTrackerConfig, error },
  };
};
